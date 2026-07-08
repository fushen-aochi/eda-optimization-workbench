#include "kernel/register.h"
#include "kernel/log.h"
#include "kernel/sigtools.h"
#include "kernel/modtools.h"
#include "kernel/celltypes.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

namespace
{
struct TimingNode
{
	RTLIL::Cell *cell = nullptr;
	std::vector<RTLIL::Cell*> fanin;
	std::vector<RTLIL::Cell*> fanout;
	RTLIL::Cell *critical_pred = nullptr;
	int delay = 1;
	int arrival = 0;
	int required = 0x3fffffff;
	int slack = 0x3fffffff;
};

struct TimingEndpoint
{
	std::string name;
	RTLIL::Cell *driver = nullptr;
	int arrival = 0;
	int required = 0;
	int slack = 0;
};

struct TimingModel
{
	std::map<RTLIL::IdString, int> override_delay;
	std::map<RTLIL::IdString, int> node_delay;
	int default_delay = 1;
	int clock_period = -1;
	int topn = 10;
	int critical_slack = 0;
	bool show_all = false;
};

bool is_sequential_cell(RTLIL::Cell *cell)
{
	return cell->type.in(ID($ff), ID($dff), ID($adff), ID($sdff), ID($dffe), ID($adffe), ID($sdffe),
	                     ID($dffsr), ID($dffsre), ID($dlatch), ID($adlatch), ID($sr));
}

bool is_sequential_endpoint_port(RTLIL::IdString port)
{
	return port == ID::D || port == ID::A || port == ID::B;
}

int log2_ceil_int(int value)
{
	int out = 0;
	int p = 1;
	while (p < value) {
		p <<= 1;
		out++;
	}
	return out;
}

int cell_width(RTLIL::Cell *cell)
{
	int width = 1;
	for (auto &conn : cell->connections())
		width = std::max(width, GetSize(conn.second));
	return width;
}

int builtin_delay(RTLIL::Cell *cell, int fanout, int default_delay)
{
	int width_cost = log2_ceil_int(std::max(1, cell_width(cell)));
	int delay = default_delay;

	if (cell->type.in(ID($not), ID($pos), ID($logic_not), ID($reduce_bool)))
		delay = 1;
	else if (cell->type.in(ID($and), ID($or), ID($xor), ID($xnor), ID($logic_and), ID($logic_or),
	                       ID($reduce_and), ID($reduce_or), ID($reduce_xor), ID($reduce_xnor)))
		delay = 1 + std::max(0, width_cost - 1);
	else if (cell->type.in(ID($mux), ID($pmux)))
		delay = 2 + width_cost;
	else if (cell->type.in(ID($eq), ID($ne), ID($eqx), ID($nex), ID($lt), ID($le), ID($ge), ID($gt)))
		delay = 2 + width_cost;
	else if (cell->type.in(ID($add), ID($sub), ID($alu)))
		delay = 2 + width_cost;
	else if (cell->type.in(ID($shl), ID($shr), ID($sshl), ID($sshr), ID($shift), ID($shiftx)))
		delay = 3 + width_cost;
	else if (cell->type.in(ID($mul)))
		delay = 4 + 2 * width_cost;
	else if (cell->type.in(ID($div), ID($mod), ID($pow)))
		delay = 8 + 2 * width_cost;
	else if (cell->type.in(ID($memrd), ID($memwr), ID($meminit), ID($mem)))
		delay = 4 + width_cost;

	if (fanout > 4)
		delay += (fanout - 1) / 4;
	return delay;
}

int cell_delay(RTLIL::Cell *cell, int fanout, const TimingModel &model)
{
	for (auto &conn : cell->connections()) {
		if (conn.first != ID::Y)
			continue;
		for (auto bit : conn.second) {
			if (bit.wire == nullptr)
				continue;
			auto it = model.node_delay.find(bit.wire->name);
			if (it != model.node_delay.end())
				return it->second;
		}
	}
	auto it = model.override_delay.find(cell->type);
	if (it != model.override_delay.end())
		return it->second;
	return builtin_delay(cell, fanout, model.default_delay);
}

bool is_supported_timing_cell(RTLIL::Cell *cell, const CellTypes &ct)
{
	return ct.cell_known(cell->type) && !is_sequential_cell(cell);
}

std::vector<SigBit> output_bits(RTLIL::Cell *cell, const SigMap &sigmap, const CellTypes &ct)
{
	std::vector<SigBit> bits;
	if (!ct.cell_known(cell->type))
		return bits;
	for (auto &conn : cell->connections()) {
		if (!ct.cell_output(cell->type, conn.first))
			continue;
		for (auto bit : sigmap(conn.second))
			bits.push_back(bit);
	}
	return bits;
}

std::vector<SigBit> input_bits(RTLIL::Cell *cell, const SigMap &sigmap, const CellTypes &ct)
{
	std::vector<SigBit> bits;
	if (!ct.cell_known(cell->type))
		return bits;
	for (auto &conn : cell->connections()) {
		if (is_sequential_cell(cell)) {
			if (!is_sequential_endpoint_port(conn.first))
				continue;
		} else if (!ct.cell_input(cell->type, conn.first)) {
			continue;
		}
		for (auto bit : sigmap(conn.second))
			bits.push_back(bit);
	}
	return bits;
}

std::string output_node_name(RTLIL::Cell *cell)
{
	if (cell == nullptr)
		return "<source>";
	auto it = cell->connections().find(ID::Y);
	if (it == cell->connections().end())
		return log_id(cell->name);
	for (auto bit : it->second) {
		if (bit.wire != nullptr)
			return log_id(bit.wire->name);
	}
	return log_id(cell->name);
}

std::string cell_label(RTLIL::Cell *cell)
{
	if (cell == nullptr)
		return "<source>";
	return stringf("%s:%s", output_node_name(cell).c_str(), log_id(cell->type));
}

void build_driver_map(RTLIL::Module *module, const SigMap &sigmap, const CellTypes &ct, dict<SigBit, RTLIL::Cell*> &driver)
{
	for (auto cell : module->cells()) {
		for (auto bit : output_bits(cell, sigmap, ct))
			driver[bit] = cell;
	}
}

void build_timing_graph(RTLIL::Module *module,
                        const TimingModel &model,
                        const SigMap &sigmap,
                        const CellTypes &ct,
                        const dict<SigBit, RTLIL::Cell*> &driver,
                        std::vector<RTLIL::Cell*> &cells,
                        std::map<RTLIL::Cell*, TimingNode> &nodes,
                        std::map<RTLIL::Cell*, int> &fanout)
{
	for (auto cell : module->selected_cells()) {
		if (!is_supported_timing_cell(cell, ct))
			continue;
		cells.push_back(cell);
		nodes[cell].cell = cell;
		fanout[cell] = 0;
	}

	for (auto cell : module->cells()) {
		for (auto bit : input_bits(cell, sigmap, ct)) {
			auto it = driver.find(bit);
			if (it != driver.end() && nodes.count(it->second))
				fanout[it->second]++;
		}
	}
	for (auto wire : module->wires()) {
		if (!wire->port_output)
			continue;
		for (auto bit : sigmap(SigSpec(wire))) {
			auto it = driver.find(bit);
			if (it != driver.end() && nodes.count(it->second))
				fanout[it->second]++;
		}
	}

	for (auto cell : cells)
		nodes[cell].delay = cell_delay(cell, fanout[cell], model);

	for (auto cell : cells) {
		std::set<RTLIL::Cell*> unique_fanin;
		for (auto bit : input_bits(cell, sigmap, ct)) {
			auto it = driver.find(bit);
			if (it == driver.end() || it->second == cell || nodes.count(it->second) == 0)
				continue;
			unique_fanin.insert(it->second);
		}
		for (auto pred : unique_fanin) {
			nodes[cell].fanin.push_back(pred);
			nodes[pred].fanout.push_back(cell);
		}
	}
}

std::vector<RTLIL::Cell*> topological_cells(const std::vector<RTLIL::Cell*> &cells, std::map<RTLIL::Cell*, TimingNode> &nodes)
{
	std::map<RTLIL::Cell*, int> indegree;
	for (auto cell : cells)
		indegree[cell] = (int)std::set<RTLIL::Cell*>(nodes[cell].fanin.begin(), nodes[cell].fanin.end()).size();

	std::vector<RTLIL::Cell*> ready;
	for (auto cell : cells)
		if (indegree[cell] == 0)
			ready.push_back(cell);

	std::vector<RTLIL::Cell*> order;
	for (size_t idx = 0; idx < ready.size(); idx++) {
		RTLIL::Cell *cell = ready[idx];
		order.push_back(cell);
		std::set<RTLIL::Cell*> unique_fanout(nodes[cell].fanout.begin(), nodes[cell].fanout.end());
		for (auto succ : unique_fanout) {
			indegree[succ]--;
			if (indegree[succ] == 0)
				ready.push_back(succ);
		}
	}

	if (order.size() == cells.size())
		return order;

	log_warning("Timing graph contains a combinational cycle or unresolved ordering; reporting with selection order.\n");
	return cells;
}

int compute_arrival(const std::vector<RTLIL::Cell*> &order, std::map<RTLIL::Cell*, TimingNode> &nodes)
{
	int worst = 0;
	for (auto cell : order) {
		int max_input = 0;
		RTLIL::Cell *best_pred = nullptr;
		for (auto pred : std::set<RTLIL::Cell*>(nodes[cell].fanin.begin(), nodes[cell].fanin.end())) {
			if (best_pred == nullptr || nodes[pred].arrival > max_input) {
				max_input = nodes[pred].arrival;
				best_pred = pred;
			}
		}
		nodes[cell].critical_pred = best_pred;
		nodes[cell].arrival = max_input + nodes[cell].delay;
		worst = std::max(worst, nodes[cell].arrival);
	}
	return worst;
}

std::vector<TimingEndpoint> collect_endpoints(RTLIL::Module *module,
                                              const SigMap &sigmap,
                                              const CellTypes &ct,
                                              const dict<SigBit, RTLIL::Cell*> &driver,
                                              const std::map<RTLIL::Cell*, TimingNode> &nodes)
{
	std::vector<TimingEndpoint> endpoints;
	for (auto wire : module->wires()) {
		if (!wire->port_output)
			continue;
		int bit_index = 0;
		for (auto bit : sigmap(SigSpec(wire))) {
			TimingEndpoint endpoint;
			endpoint.name = stringf("output %s[%d]", log_id(wire->name), bit_index++);
			auto it = driver.find(bit);
			if (it != driver.end() && nodes.count(it->second)) {
				endpoint.driver = it->second;
				endpoint.arrival = nodes.at(it->second).arrival;
			}
			endpoints.push_back(endpoint);
		}
	}

	for (auto cell : module->cells()) {
		if (!is_sequential_cell(cell))
			continue;
		int bit_index = 0;
		for (auto bit : input_bits(cell, sigmap, ct)) {
			TimingEndpoint endpoint;
			endpoint.name = stringf("seq %s/D[%d]", log_id(cell->name), bit_index++);
			auto it = driver.find(bit);
			if (it != driver.end() && nodes.count(it->second)) {
				endpoint.driver = it->second;
				endpoint.arrival = nodes.at(it->second).arrival;
			}
			endpoints.push_back(endpoint);
		}
	}

	for (auto &item : nodes) {
		if (!item.second.fanout.empty())
			continue;
		bool already_endpoint = false;
		for (auto &endpoint : endpoints) {
			if (endpoint.driver == item.first) {
				already_endpoint = true;
				break;
			}
		}
		if (!already_endpoint) {
			TimingEndpoint endpoint;
			endpoint.name = stringf("open %s", log_id(item.first->name));
			endpoint.driver = item.first;
			endpoint.arrival = item.second.arrival;
			endpoints.push_back(endpoint);
		}
	}
	return endpoints;
}

void compute_required(const std::vector<RTLIL::Cell*> &order,
                      std::map<RTLIL::Cell*, TimingNode> &nodes,
                      std::vector<TimingEndpoint> &endpoints,
                      int required_time)
{
	for (auto cell : order)
		nodes[cell].required = 0x3fffffff;

	for (auto &endpoint : endpoints) {
		endpoint.required = required_time;
		endpoint.slack = endpoint.required - endpoint.arrival;
		if (endpoint.driver != nullptr)
			nodes[endpoint.driver].required = std::min(nodes[endpoint.driver].required, endpoint.required);
	}

	for (auto it = order.rbegin(); it != order.rend(); ++it) {
		RTLIL::Cell *cell = *it;
		if (nodes[cell].required == 0x3fffffff)
			continue;
		for (auto pred : std::set<RTLIL::Cell*>(nodes[cell].fanin.begin(), nodes[cell].fanin.end()))
			nodes[pred].required = std::min(nodes[pred].required, nodes[cell].required - nodes[cell].delay);
	}

	for (auto cell : order) {
		if (nodes[cell].required == 0x3fffffff)
			nodes[cell].required = required_time;
		nodes[cell].slack = nodes[cell].required - nodes[cell].arrival;
	}
}

std::vector<RTLIL::Cell*> reconstruct_path(RTLIL::Cell *end, std::map<RTLIL::Cell*, TimingNode> &nodes)
{
	std::vector<RTLIL::Cell*> path;
	std::set<RTLIL::Cell*> seen;
	for (auto cell = end; cell != nullptr && seen.count(cell) == 0; cell = nodes[cell].critical_pred) {
		seen.insert(cell);
		path.push_back(cell);
	}
	std::reverse(path.begin(), path.end());
	return path;
}

void report_timing(RTLIL::Module *module,
                   const std::vector<RTLIL::Cell*> &order,
                   std::map<RTLIL::Cell*, TimingNode> &nodes,
                   std::vector<TimingEndpoint> &endpoints,
                   const std::map<RTLIL::Cell*, int> &fanout,
                   int required_time,
                   const TimingModel &model)
{
	std::sort(endpoints.begin(), endpoints.end(), [](const TimingEndpoint &a, const TimingEndpoint &b) {
		if (a.slack != b.slack)
			return a.slack < b.slack;
		return a.arrival > b.arrival;
	});

	int worst_arrival = 0;
	for (auto &endpoint : endpoints)
		worst_arrival = std::max(worst_arrival, endpoint.arrival);

	log("  Module %s timing summary:\n", log_id(module));
	log("    timed combinational cells: %d\n", (int)order.size());
	log("    endpoints: %d\n", (int)endpoints.size());
	log("    required time: %d%s\n", required_time, model.clock_period > 0 ? "" : " (auto = worst arrival)");
	log("    worst arrival: %d\n", worst_arrival);
	if (!endpoints.empty())
		log("    worst slack: %d at %s\n", endpoints.front().slack, endpoints.front().name.c_str());

	if (!endpoints.empty()) {
		log("  Critical path:\n");
		std::vector<RTLIL::Cell*> path = reconstruct_path(endpoints.front().driver, nodes);
		if (path.empty()) {
			log("    <direct source to %s>\n", endpoints.front().name.c_str());
		} else {
			for (auto cell : path) {
				auto &node = nodes[cell];
				log("    %s  delay=%d arrival=%d required=%d slack=%d\n",
				    cell_label(cell).c_str(), node.delay, node.arrival, node.required, node.slack);
			}
			log("    endpoint %s  arrival=%d required=%d slack=%d\n",
			    endpoints.front().name.c_str(), endpoints.front().arrival, endpoints.front().required, endpoints.front().slack);
		}
	}

	std::vector<RTLIL::Cell*> cells_by_slack = order;
	std::sort(cells_by_slack.begin(), cells_by_slack.end(), [&](RTLIL::Cell *a, RTLIL::Cell *b) {
		if (nodes[a].slack != nodes[b].slack)
			return nodes[a].slack < nodes[b].slack;
		return nodes[a].arrival > nodes[b].arrival;
	});

	log("  Critical nodes (slack <= %d):\n", model.critical_slack);
	int critical_count = 0;
	for (auto cell : cells_by_slack) {
		if (nodes[cell].slack > model.critical_slack)
			continue;
		critical_count++;
		log("    %s  delay=%d arrival=%d required=%d slack=%d fanout=%d\n",
		    cell_label(cell).c_str(), nodes[cell].delay, nodes[cell].arrival, nodes[cell].required,
		    nodes[cell].slack, fanout.at(cell));
	}
	if (critical_count == 0)
		log("    <none>\n");

	log("  Lowest-slack endpoints:\n");
	for (int i = 0; i < std::min(model.topn, (int)endpoints.size()); i++)
		log("    %s  arrival=%d required=%d slack=%d\n",
		    endpoints[i].name.c_str(), endpoints[i].arrival, endpoints[i].required, endpoints[i].slack);

	log("  Lowest-slack cells:\n");
	for (int i = 0; i < std::min(model.topn, (int)cells_by_slack.size()); i++) {
		auto cell = cells_by_slack[i];
		log("    %s  delay=%d arrival=%d required=%d slack=%d fanout=%d\n",
		    cell_label(cell).c_str(), nodes[cell].delay, nodes[cell].arrival, nodes[cell].required,
		    nodes[cell].slack, fanout.at(cell));
	}

	if (model.show_all) {
		log("  All timed cells:\n");
		std::vector<RTLIL::Cell*> by_name = order;
		std::sort(by_name.begin(), by_name.end(), [](RTLIL::Cell *a, RTLIL::Cell *b) {
			return log_id(a->name) < log_id(b->name);
		});
		for (auto cell : by_name)
			log("    %s  delay=%d arrival=%d required=%d slack=%d fanout=%d\n",
			    cell_label(cell).c_str(), nodes[cell].delay, nodes[cell].arrival, nodes[cell].required,
			    nodes[cell].slack, fanout.at(cell));
	}
}
} // namespace

struct EDAOptTimingPass : public Pass
{
	EDAOptTimingPass() : Pass("my_opt_timing", "static timing analysis with critical path, critical node and slack report") {}

	void help() override
	{
		log("\n");
		log("    my_opt_timing [options] [selection]\n");
		log("\n");
		log("Build a structural combinational timing graph and report delay, arrival\n");
		log("time, required time, slack, critical endpoints, critical nodes, and one\n");
		log("reconstructed critical path. Sequential cells are timing boundaries.\n");
		log("\n");
		log("    -clock <N>\n");
		log("        endpoint required time. Default: worst arrival, so worst slack is 0.\n");
		log("\n");
		log("    -period <N>\n");
		log("        alias for -clock.\n");
		log("\n");
		log("    -default_delay <N>\n");
		log("        fallback delay for supported cells without a specific model. Default: 1.\n");
		log("\n");
		log("    -delay <celltype> <N>\n");
		log("        override delay for a cell type, e.g. -delay $mul 5.\n");
		log("\n");
		log("    -node_delay <wire> <N>\n");
		log("        override delay for the cell driving a named wire, e.g. -node_delay K 7.\n");
		log("\n");
		log("    -topn <N>\n");
		log("        number of low-slack cells and endpoints to print. Default: 10.\n");
		log("\n");
		log("    -critical_slack <N>\n");
		log("        classify cells with slack <= N as critical. Default: 0.\n");
		log("\n");
		log("    -show_all\n");
		log("        print every timed cell after the summary.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		TimingModel model;
		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			if ((args[argidx] == "-clock" || args[argidx] == "-period") && argidx + 1 < args.size()) {
				model.clock_period = atoi(args[++argidx].c_str());
				continue;
			}
			if (args[argidx] == "-default_delay" && argidx + 1 < args.size()) {
				model.default_delay = atoi(args[++argidx].c_str());
				continue;
			}
			if (args[argidx] == "-delay" && argidx + 2 < args.size()) {
				RTLIL::IdString type = RTLIL::escape_id(args[++argidx]);
				model.override_delay[type] = atoi(args[++argidx].c_str());
				continue;
			}
			if (args[argidx] == "-node_delay" && argidx + 2 < args.size()) {
				RTLIL::IdString wire = RTLIL::escape_id(args[++argidx]);
				model.node_delay[wire] = atoi(args[++argidx].c_str());
				continue;
			}
			if (args[argidx] == "-topn" && argidx + 1 < args.size()) {
				model.topn = atoi(args[++argidx].c_str());
				continue;
			}
			if (args[argidx] == "-critical_slack" && argidx + 1 < args.size()) {
				model.critical_slack = atoi(args[++argidx].c_str());
				continue;
			}
			if (args[argidx] == "-show_all") {
				model.show_all = true;
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);

		if (model.clock_period == 0 || model.clock_period < -1)
			log_cmd_error("my_opt_timing: -clock/-period must be positive or omitted.\n");
		if (model.default_delay < 1)
			log_cmd_error("my_opt_timing: -default_delay must be positive.\n");
		if (model.topn < 1)
			log_cmd_error("my_opt_timing: -topn must be positive.\n");
		for (auto &item : model.override_delay)
			if (item.second < 1)
				log_cmd_error("my_opt_timing: -delay values must be positive.\n");
		for (auto &item : model.node_delay)
			if (item.second < 1)
				log_cmd_error("my_opt_timing: -node_delay values must be positive.\n");

		log_header(design, "Executing my_opt_TIMING pass (critical path and slack analysis).\n");

		for (auto module : design->selected_whole_modules_warn()) {
			if (module->has_processes_warn())
				continue;

			SigMap sigmap(module);
			CellTypes ct;
			ct.setup_internals();
			dict<SigBit, RTLIL::Cell*> driver;
			std::vector<RTLIL::Cell*> cells;
			std::map<RTLIL::Cell*, TimingNode> nodes;
			std::map<RTLIL::Cell*, int> fanout;

			build_driver_map(module, sigmap, ct, driver);
			build_timing_graph(module, model, sigmap, ct, driver, cells, nodes, fanout);
			if (cells.empty()) {
				log("  Module %s: no supported combinational timing cells selected.\n", log_id(module));
				continue;
			}

			std::vector<RTLIL::Cell*> order = topological_cells(cells, nodes);
			int worst_arrival = compute_arrival(order, nodes);
			std::vector<TimingEndpoint> endpoints = collect_endpoints(module, sigmap, ct, driver, nodes);
			int required_time = model.clock_period > 0 ? model.clock_period : worst_arrival;
			compute_required(order, nodes, endpoints, required_time);
			report_timing(module, order, nodes, endpoints, fanout, required_time, model);
		}

		log_header(design, "my_opt_TIMING has been done!\n");
	}
} EDAOptTimingPass;

PRIVATE_NAMESPACE_END
