module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <concepts>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <utility>
#include <vector>

export module forge.chain.fork.graph;

export import forge.chain.fork.types;
export import forge.chain.fork.exceptions;

export namespace forge::chain::fork {

template <typename Value>
concept graph_value = std::copy_constructible<Value> && std::movable<Value>;

template <std::totally_ordered Id, std::totally_ordered Rank, graph_value Value,
          typename Better = std::greater<Rank>>
class graph {
 public:
   struct entry {
      Id id;
      Id parent;
      Rank rank;
      Value value;
   };

   explicit graph(Better better = {})
       : better_{std::make_shared<const Better>(std::move(better))},
         ranking_{rank_compare{better_}} {}

   void reset(entry root) {
      auto nodes = node_map{};
      nodes.emplace(root.id, node{.data = root});
      auto ranking = rank_set{rank_compare{better_}};
      ranking.emplace(root.rank, root.id);

      auto lock = std::unique_lock{mutex_};
      nodes_.swap(nodes);
      ranking_.swap(ranking);
      root_ = std::move(root.id);
   }

   [[nodiscard]] insert_status insert(entry value) {
      auto lock = std::unique_lock{mutex_};
      if (nodes_.contains(value.id)) {
         return insert_status::duplicate;
      }
      const auto parent = nodes_.find(value.parent);
      if (parent == nodes_.end()) {
         return insert_status::unlinked;
      }

      const auto id = value.id;
      const auto rank = value.rank;
      const auto [inserted_node, inserted] = nodes_.emplace(id, node{.data = std::move(value)});
      static_cast<void>(inserted);
      auto ranked = ranking_.end();
      try {
         ranked = ranking_.emplace(rank, id).first;
         parent->second.children.insert(id);
      } catch (...) {
         if (ranked != ranking_.end()) {
            ranking_.erase(ranked);
         }
         nodes_.erase(inserted_node);
         throw;
      }
      return insert_status::inserted;
   }

   [[nodiscard]] bool contains(const Id& id) const {
      auto lock = std::shared_lock{mutex_};
      return nodes_.contains(id);
   }

   [[nodiscard]] std::optional<entry> find(const Id& id) const {
      auto lock = std::shared_lock{mutex_};
      const auto found = nodes_.find(id);
      return found == nodes_.end() ? std::nullopt : std::optional<entry>{found->second.data};
   }

   void replace(entry value) {
      auto lock = std::unique_lock{mutex_};
      auto& current = require_node_unlocked(value.id);
      if (current.data.parent != value.parent || current.data.rank != value.rank) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_node, "fork graph replacement cannot change parent or rank");
      }
      current.data.value = std::move(value.value);
   }

   [[nodiscard]] entry root() const {
      auto lock = std::shared_lock{mutex_};
      return require_node_unlocked(require_root_unlocked()).data;
   }

   [[nodiscard]] entry best() const {
      auto lock = std::shared_lock{mutex_};
      (void)require_root_unlocked();
      return require_node_unlocked(ranking_.begin()->id).data;
   }

   [[nodiscard]] bool is_ancestor(const Id& ancestor, const Id& descendant) const {
      auto lock = std::shared_lock{mutex_};
      auto current = require_node_unlocked(descendant).data.id;
      while (true) {
         if (current == ancestor) {
            return true;
         }
         const auto& item = require_node_unlocked(current).data;
         if (item.id == require_root_unlocked()) {
            return false;
         }
         current = item.parent;
      }
   }

   [[nodiscard]] Id common_ancestor(const Id& left, const Id& right) const {
      auto lock = std::shared_lock{mutex_};
      auto ancestors = std::set<Id>{};
      auto cursor = left;
      while (true) {
         ancestors.insert(cursor);
         const auto& item = require_node_unlocked(cursor).data;
         if (cursor == require_root_unlocked()) {
            break;
         }
         cursor = item.parent;
      }

      cursor = right;
      while (!ancestors.contains(cursor)) {
         const auto& item = require_node_unlocked(cursor).data;
         if (cursor == require_root_unlocked()) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_root, "fork branches do not share the configured root");
         }
         cursor = item.parent;
      }
      return cursor;
   }

   [[nodiscard]] std::vector<entry> path(const Id& ancestor, const Id& descendant) const {
      auto lock = std::shared_lock{mutex_};
      (void)require_node_unlocked(ancestor);
      (void)require_node_unlocked(descendant);
      auto reversed = std::vector<entry>{};
      auto cursor = descendant;
      while (cursor != ancestor) {
         const auto& item = require_node_unlocked(cursor).data;
         reversed.push_back(item);
         if (cursor == require_root_unlocked()) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_root, "fork path does not descend from requested ancestor");
         }
         cursor = item.parent;
      }
      std::ranges::reverse(reversed);
      return reversed;
   }

   [[nodiscard]] std::vector<entry> remove_subtree(const Id& id) {
      auto lock = std::unique_lock{mutex_};
      if (id == require_root_unlocked()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_root, "fork root subtree cannot be removed");
      }
      const auto parent = require_node_unlocked(id).data.parent;
      const auto subtree = collect_subtree_unlocked(id);

      auto removed = std::vector<entry>{};
      removed.reserve(subtree.size());
      for (const auto& child : subtree) {
         removed.push_back(require_node_unlocked(child).data);
      }

      nodes_.at(parent).children.erase(id);
      erase_nodes_unlocked(subtree);
      return removed;
   }

   [[nodiscard]] std::vector<entry> advance_root(const Id& id) {
      auto lock = std::unique_lock{mutex_};
      (void)require_node_unlocked(id);

      const auto retained = collect_subtree_unlocked(id);
      auto retained_ids = std::set<Id>{retained.begin(), retained.end()};
      auto removed = std::vector<entry>{};
      removed.reserve(nodes_.size() - retained.size());
      for (const auto& [node_id, current] : nodes_) {
         if (!retained_ids.contains(node_id)) {
            removed.push_back(current.data);
         }
      }

      auto nodes = node_map{};
      auto ranking = rank_set{rank_compare{better_}};
      for (const auto& node_id : retained) {
         auto value = require_node_unlocked(node_id).data;
         if (node_id == id) {
            value.parent = Id{};
         }
         nodes.emplace(node_id, node{.data = value});
         ranking.emplace(value.rank, value.id);
      }
      for (const auto& [node_id, current] : nodes) {
         if (node_id != id) {
            nodes.at(current.data.parent).children.insert(node_id);
         }
      }

      nodes_.swap(nodes);
      ranking_.swap(ranking);
      root_ = id;
      return removed;
   }

   [[nodiscard]] std::size_t size() const {
      auto lock = std::shared_lock{mutex_};
      return nodes_.size();
   }

 private:
   struct node {
      entry data;
      std::set<Id> children;
   };

   struct rank_key {
      Rank rank;
      Id id;
   };

   struct rank_compare {
      std::shared_ptr<const Better> better;

      bool operator()(const rank_key& left, const rank_key& right) const {
         if (std::invoke(*better, left.rank, right.rank)) {
            return true;
         }
         if (std::invoke(*better, right.rank, left.rank)) {
            return false;
         }
         return left.id > right.id;
      }
   };

   using node_map = std::map<Id, node>;
   using rank_set = std::set<rank_key, rank_compare>;

   [[nodiscard]] const Id& require_root_unlocked() const {
      if (!root_) {
         FORGE_THROW_EXCEPTION(exceptions::empty_graph, "fork graph has no root");
      }
      return *root_;
   }

   [[nodiscard]] node& require_node_unlocked(const Id& id) {
      const auto found = nodes_.find(id);
      if (found == nodes_.end()) {
         FORGE_THROW_EXCEPTION(exceptions::unknown_node, "fork graph node is unknown");
      }
      return found->second;
   }

   [[nodiscard]] const node& require_node_unlocked(const Id& id) const {
      const auto found = nodes_.find(id);
      if (found == nodes_.end()) {
         FORGE_THROW_EXCEPTION(exceptions::unknown_node, "fork graph node is unknown");
      }
      return found->second;
   }

   [[nodiscard]] std::vector<Id> collect_subtree_unlocked(const Id& id) const {
      (void)require_node_unlocked(id);
      auto result = std::vector<Id>{id};
      for (auto index = std::size_t{}; index < result.size(); ++index) {
         const auto& children = require_node_unlocked(result[index]).children;
         result.insert(result.end(), children.begin(), children.end());
      }
      return result;
   }

   void erase_nodes_unlocked(const std::vector<Id>& ids) {
      for (const auto& id : ids) {
         const auto found = nodes_.find(id);
         ranking_.erase(rank_key{found->second.data.rank, found->first});
         nodes_.erase(found);
      }
   }

   mutable std::shared_mutex mutex_;
   node_map nodes_;
   std::optional<Id> root_;
   std::shared_ptr<const Better> better_;
   rank_set ranking_;
};

} // namespace forge::chain::fork
