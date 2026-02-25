#pragma once

#include <algorithm>
#include <memory>
#include <vector>

namespace vkwave
{

/// Static per-type object tracker using weak_ptr.
/// Tracks all live instances of T. Expired weak_ptrs are garbage-collected
/// on any access. Ownership stays with whoever holds the shared_ptr —
/// when the last shared_ptr dies, the tracker forgets the object.
template<typename T>
class ObjectTracker
{
public:
  static void track(std::shared_ptr<T> ptr)
  {
    collect();
    s_objects.push_back(std::move(ptr));
  }

  /// Return the first live instance (convenience for single-instance types).
  static std::shared_ptr<T> get()
  {
    for (auto& w : s_objects)
    {
      if (auto p = w.lock())
        return p;
    }
    return nullptr;
  }

  /// Return all live instances.
  static std::vector<std::shared_ptr<T>> all()
  {
    collect();
    std::vector<std::shared_ptr<T>> result;
    for (auto& w : s_objects)
    {
      if (auto p = w.lock())
        result.push_back(p);
    }
    return result;
  }

  /// Remove expired weak_ptrs.
  static void collect()
  {
    std::erase_if(s_objects,
      [](const std::weak_ptr<T>& w) { return w.expired(); });
  }

  static size_t count()
  {
    collect();
    return s_objects.size();
  }

private:
  static inline std::vector<std::weak_ptr<T>> s_objects;
};

/// CRTP mixin — auto-tracks instances via ObjectTracker<T>.
/// Derived class makes its ctor private and befriends Tracked<Derived>.
template<typename T>
class Tracked
{
public:
  static std::shared_ptr<T> create()
  {
    auto ptr = std::shared_ptr<T>(new T());
    ObjectTracker<T>::track(ptr);
    return ptr;
  }

  static std::shared_ptr<T> get() { return ObjectTracker<T>::get(); }
  static std::vector<std::shared_ptr<T>> all() { return ObjectTracker<T>::all(); }
  static size_t count() { return ObjectTracker<T>::count(); }
};

} // namespace vkwave
