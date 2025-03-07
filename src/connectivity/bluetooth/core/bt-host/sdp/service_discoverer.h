// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_SDP_SERVICE_DISCOVERER_H_
#define SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_SDP_SERVICE_DISCOVERER_H_

#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <fbl/ref_ptr.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include "src/connectivity/bluetooth/core/bt-host/common/identifier.h"
#include "src/connectivity/bluetooth/core/bt-host/sdp/client.h"
#include "src/connectivity/bluetooth/core/bt-host/sdp/sdp.h"

namespace bt {
namespace sdp {

// The Service Discoverer keeps track of which services are of interest to
// the host, and searches for those services on a remote device when directed
// to, reporting back to those interested asynchronously.
//
// Usually only one ServiceDiscoverer will exist per host.
// This class is thread-hostile: all functions must be called on the creation
// thread.
class ServiceDiscoverer final {
 public:
  ServiceDiscoverer();

  // Destroying the ServiceDiscoverer will mean all current searches will end
  // and all current clients will be disconnected.
  ~ServiceDiscoverer() = default;

  using SearchId = uint64_t;

  constexpr static SearchId kInvalidSearchId = 0u;

  // Add an interest in discovering a remote service.
  // Discoverer will search for services with |uuid| in their records, and
  // return via |callback| all of the attributes specified in |attributes|,
  // along with the peer's |device_id|.
  // If |attributes| is empty, all attributes will be requested.
  // Returns a SearchId can be used to remove the search later if successful,
  // or kInvalidSearchId if adding the search failed.
  // |callback| will be called on the creation thread of ServiceDiscoverer.
  using ResultCallback = fit::function<void(
      common::DeviceId, const std::map<AttributeId, DataElement> &)>;
  SearchId AddSearch(const common::UUID &uuid,
                     std::unordered_set<AttributeId> attributes,
                     ResultCallback callback);

  // Remove a search previously added with AddSearch().
  // Returns true if a search was removed and false if it was not found.
  // This function is idempotent.
  bool RemoveSearch(SearchId id);

  // Searches for all the registered services using a SDP |client|.
  // asynchronously.  The client is destroyed (disconnected) afterwards.
  // If a search is already being performed on the same |peer_id|, the client
  // is immediately dropped.
  // Returns true if discovery was started, and false otherwise.
  bool StartServiceDiscovery(common::DeviceId peer_id,
                             std::unique_ptr<Client> client);

 private:
  // A registered search.
  struct Search {
    common::UUID uuid;
    std::unordered_set<AttributeId> attributes;
    ResultCallback callback;
  };

  // A Discovery Session happens using a Client, and ends when no registered
  // searches still need to be completed.
  struct DiscoverySession {
    std::unique_ptr<Client> client;
    // The set of Searches that have yet to complete.
    // Should always be non-empty if this session exists.
    std::unordered_set<SearchId> active;
  };

  // Finish the Discovery Session for |peer_id| searching |search_id|,
  // releasing the client if all searches are complete.
  void FinishPeerSearch(common::DeviceId peer_id, SearchId search_id);

  // Next likely search id
  SearchId next_id_;

  // Registered searches
  std::unordered_map<SearchId, Search> searches_;

  // Clients that searches are still being performed on, based on the remote
  // peer id.
  std::unordered_map<common::DeviceId, DiscoverySession> sessions_;

  fxl::ThreadChecker thread_checker_;
};

}  // namespace sdp
}  // namespace bt

#endif  // SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_SDP_SERVICE_DISCOVERER_H_
