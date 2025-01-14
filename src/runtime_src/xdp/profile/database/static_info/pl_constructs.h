/**
 * Copyright (C) 2021-2022 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#ifndef PL_CONSTRUCTS_DOT_H
#define PL_CONSTRUCTS_DOT_H

// This file collects all of the data structures used in the static info
//  database for constructs that exist in the PL portion of the design.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// For DEBUG_IP_TYPE
#include "core/include/xclbin.h"

#include "xdp/config.h"
#include "xdp/profile/device/utility.h"

namespace xdp {

  // The Monitor struct collects the information on a single
  //  Accelerator Monitor (AM), AXI Interface Monitor (AIM), or
  //  AXI Stream Monitor (ASM).  Every xclbin will have a different
  //  number of each
  struct Monitor {
    DEBUG_IP_TYPE type ; // AM, AIM, or ASM -- [[maybe_unused]]

    // At compile time, all monitors are assigned an index from MIN to MAX
    //  for each different monitor type.  This number is used to determine
    //  the slot index.  We store the original index from the debug_ip_layout
    //  here, but it may not be used.  [[maybe_unused]]
    uint64_t index ;

    // Each monitor can be configured either as counters only, or as
    //  counters + trace during compilation.  This keeps track of that
    //  information and is set based on the properties field in debug_ip_layout 
    bool traceEnabled = false ;

    // The index of the Compute Unit in the IP_LAYOUT section of the xclbin
    //  this Monitor is attached to (if any).
    int32_t cuIndex ;

    // The index of the Memory in the MEMORY_TOPOLOGY section of the xclbin
    //  this Monitor is attached to (if any)
    int32_t memIndex ;

    // The name of the Monitor from DEBUG_IP_LAYOUT.  This has parsable
    //  information on the connection or compute unit being monitored
    std::string name ;

    // The OpenCL arguments associated with this Monitor (if any).  This
    //  information is only filled in by OpenCL applications as it requires
    //  information not in the xclbin and only in the OpenCL meta-data.
    std::string args = "" ;

    // The name of the port on the associated compute unit this Monitor
    //  is attached to (if any).  This information is only filled in
    //  by OpenCL applications as it requires information not in the xclbin
    //  and only in the OpenCL meta-data.
    std::string port = "" ;

    // The bit-width of the port on the associated compute unit this Monitor
    //  is attached to (if any).  This information is only filled in
    //  by OpenCL applications as it requires information not in the xclbin
    //  and only in the OpenCL meta-data.
    uint64_t portWidth = 0 ;

    // For ASMs only, we need to keep track of if the stream we are monitoring
    //  should count as a read or a write since stream transactions could
    //  be interpreted as either.
    bool isStreamRead = false ;

    // When counters are read from the device, they are stored in a structure
    //  called xclCounterResults.  That structure has arrays of values and
    //  each monitor uses this slotIndex to index the arrays to grab the
    //  counter values for itself.
    uint64_t slotIndex ;

    // Some platforms have monitors inside the shell.  These are configured
    //  for counters only and are identified by their name.  To minimize
    //  string comparisons, we check the name in the constructor and set
    //  this boolean.
    bool shellMonitor = false ;
    inline bool isShellMonitor() const { return shellMonitor ; }

    Monitor(DEBUG_IP_TYPE ty, uint64_t idx, const char* n,
            int32_t cuId = -1, int32_t memId = -1)
      : type(ty)
      , index(idx)
      , cuIndex(cuId)
      , memIndex(memId)
      , name(n)
    {
      // The slot index is determined by the index from the debug_ip_layout,
      //  but the index in the debug_ip_layout is incremented based on the
      //  number of possible trace ids that could be generated by the
      //  monitor in hardware
      switch (ty) {
      case ACCEL_MONITOR:
        slotIndex = getAMSlotId(idx);
        break ;
      case AXI_MM_MONITOR:
        slotIndex = getAIMSlotId(idx);
        break ;
      case AXI_STREAM_MONITOR:
        slotIndex = getASMSlotId(idx);
        break ;
      default:
        // Should never be reached
        slotIndex = 0 ;
        break ;
      }

      shellMonitor = (name.find("Host to Device")   != std::string::npos) ||
                     (name.find("Peer to Peer")     != std::string::npos) ||
                     (name.find("Memory to Memory") != std::string::npos) ;
    }

  } ;

  // The ComputeUnitInstance class collects all of the information on
  //  a specific compute unit of a kernel.  Each xclbin will have a 
  //  different combination of compute units, and this is orthogonal to
  //  what is being monitored.  By keeping track of all compute units
  //  we can explicitly show what hardware resources exist that are and
  //  are not monitored.
  class ComputeUnitInstance
  {
  private:
    // The index of the Compute Unit in the IP_LAYOUT section of the xclbin
    int32_t index ;

    // The name of the compute unit, parsed out of the portion of the
    //  name in the IP_LAYOUT section after the ':'
    std::string name ;

    // The name of the kernel this compute unit belongs to, parsed out of the
    //  portion of the name in the IP_LAYOUT section before the ':'
    std::string kernelName ;

    // OpenCL compute units have x, y, and z dimensions to create a
    //  workgroup size.  Non-OpenCL compute units do not report this information
    int32_t dim[3] = { 0, 0, 0 } ;

    // If this compute unit was built in HLS with stalls enabled
    bool stall = false ;

    // If this compute unit was built in HLS with dataflow enabled
    bool dataflow = false ;

    // If this compute unit has a Fast Adapter attached
    bool hasFA = false ;

    // In hardware, each compute unit port can connect to any number of
    //  memory resources (like DDR and PLRAM).  This map keeps track of
    //  which memory resources each argument is connected to and is built
    //  using information in the CONNECTIVITY section of the xclbin
    std::map<int32_t, std::vector<int32_t>> connections ;

    // If this compute unit has an AM attached to it, then the amId
    //  is keeping track of the slot ID inside the xclCounterResults
    //  structure for this compute unit.
    int32_t amId = -1 ;

    // If this compute unit has any AIMs or ASMs attached to its ports,
    //  then these vectors will keep track of the slot IDs inside the
    //  xclCounterResults structure for all of the attached monitors.
    std::vector<uint32_t> aimIds ; // All AIMs (counters only and trace enabled)
    std::vector<uint32_t> asmIds ; // All ASMs (counters only and trace enabled)
    std::vector<uint32_t> aimIdsWithTrace ; // Only AIMs with trace
    std::vector<uint32_t> asmIdsWithTrace ; // Only ASMs with trace

    ComputeUnitInstance() = delete ;
  public:

    // Inlined Getters
    inline const std::string& getName()       { return name ; }
    inline const std::string& getKernelName() { return kernelName ; }
    inline const int32_t getIndex() const     { return index ; }
    inline auto getConnections()              { return &connections ; }
    inline int32_t getAccelMon() const        { return amId ; }
    inline auto getAIMs()                     { return &aimIds ; }
    inline auto getASMs()                     { return &asmIds ; }
    inline auto getAIMsWithTrace()            { return &aimIdsWithTrace ; }
    inline auto getASMsWithTrace()            { return &asmIdsWithTrace ; }
    inline bool getStallEnabled() const       { return stall ; }
    inline bool getStreamTraceEnabled() const { return asmIdsWithTrace.size() > 0 ; }
    inline bool getDataflowEnabled() const    { return dataflow ; }
    inline bool getHasFA() const              { return hasFA ; }
    inline bool getDataTransferTraceEnabled() const 
      { return aimIdsWithTrace.size() > 0 ; }

    // Inlined Setters
    inline void setDim(int32_t x, int32_t y, int32_t z)
      { dim[0] = x ; dim[1] = y ; dim[2] = z ; }
    inline void setAccelMon(int32_t id)    { amId = id ; }
    inline void setStallEnabled(bool b)    { stall = b ; }
    inline void setDataflowEnabled(bool b) { dataflow = b ; }
    inline void setFaEnabled(bool b)       { hasFA = b ; }

    // Other functions
    inline void addAIM(uint32_t id, bool trace = false)
      { aimIds.push_back(id) ; if (trace) aimIdsWithTrace.push_back(id) ; }
    inline void addASM(uint32_t id, bool trace = false)
      { asmIds.push_back(id) ; if (trace) asmIdsWithTrace.push_back(id) ; }

    XDP_EXPORT std::string getDim() ; // Construct a string from the dimensions
    XDP_EXPORT void addConnection(int32_t argIdx, int32_t memIdx) ;

    XDP_EXPORT explicit ComputeUnitInstance(int32_t i, const std::string& n) ;
    XDP_EXPORT ~ComputeUnitInstance() = default ;
  } ;

  // The Memory struct collects all of the information on a single
  //  hardware memory resource (DDR bank, HBM, PLRAM, etc.).  Each
  //  platform has a set number of resources, but each xclbin will
  //  only use a subset of the total memory.
  struct Memory
  {
    // The type of the memory resource copied from the MEM_TOPOLOGY section.
    //  This is inconsistent in some xclbins and should be unused
    //  [[maybe_unused]]
    uint8_t type ;

    // The index of the memory resource in the MEM_TOPOLOGY section
    //  [[maybe_unused]]
    int32_t index ;

    // The start physical address on the device for this memory resource
    uint64_t baseAddress ;

    // The size (in bytes) of the memory resource on the device
    uint64_t size ;

    // The name of the memory resource (as defined by the "tag" in
    //  the MEM_TOPOLOGY section)
    std::string name ;

    // A memory resource is considered "used" in an xclbin if it is
    //  connected to either a compute unit or the host memory.  This is
    //  set based on information in the MEM_TOPOLOGY section
    bool used ;

    Memory(uint8_t ty, int32_t idx, uint64_t baseAddr, uint64_t sz,
           const char* n, bool u)
      : type(ty)
      , index(idx)
      , baseAddress(baseAddr)
      , size(sz)
      , name(n)
      , used(u)
    {
    }
  } ;
  
} // end namespace xdp

#endif
