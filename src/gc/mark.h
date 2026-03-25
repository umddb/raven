#pragma once

// A seprate file for the mark phase of the mark-sweep algorithm.
// Unlike other parallel workloads, this requires workload rebalancing
// because there is one root vertex. Rebalancing will take place in form of 
// basic work stealing algorithm