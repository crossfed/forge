module;

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

export module forge.vm.wasm.vector;

export import forge.vm.wasm.allocator;
export import forge.vm.wasm.exceptions;

export namespace forge::vm::wasm {
namespace detail {
template <typename T, typename Allocator> class vector {
 public:
   constexpr vector(Allocator& allocator, size_t size = 0)
       : _size(size), _capacity(size), _allocator(&allocator), _data(allocator.template alloc<T>(_capacity)) {}

   constexpr vector(const vector& mv) = delete;
   constexpr vector(vector&& mv) = default;
   constexpr vector& operator=(vector&& mv) = default;

   constexpr inline void resize(size_t size) {
      if (size > _capacity) {
         reserve(size);
      } else if (size < _capacity) {
         _allocator->template reclaim<T>(_data + size, _capacity - size);
         _capacity = size;
      }
      _size = size;
      _index = std::min(_index, _size);
   }
   template <typename U, typename = std::enable_if_t<std::is_same_v<T, std::decay_t<U>>, int>>
   constexpr inline void push_back(U&& val) {
      // if the vector is unbounded don't assert
      if (_index >= _capacity)
         reserve(std::max<std::size_t>(1, _capacity * 2));
      _data[_index++] = std::forward<U>(val);
      _size = std::max(_size, _index);
   }
   constexpr inline void emplace_back(T&& val) {
      // if the vector is unbounded don't assert
      if (_index >= _capacity)
         reserve(std::max<std::size_t>(1, _capacity * 2));
      _data[_index++] = std::move(val);
      _size = std::max(_size, _index);
   }

   constexpr inline T& back() {
      detail::check<exceptions::vector_out_of_bounds>((_index > 0), "vector back out of bounds");
      return _data[_index - 1];
   }

   constexpr inline void pop_back() {
      detail::check<exceptions::vector_out_of_bounds>((_index > 0), "vector pop out of bounds");
      const auto previous_index = _index;
      _index--;
      if (_size == previous_index)
         _size = _index;
   }

   constexpr inline T& at(size_t i) {
      detail::check<exceptions::vector_out_of_bounds>((i < _size), "vector read out of bounds");
      return _data[i];
   }

   constexpr inline T& at(size_t i) const {
      detail::check<exceptions::vector_out_of_bounds>((i < _size), "vector read out of bounds");
      return _data[i];
   }

   constexpr inline T& at_no_check(size_t i) {
      return _data[i];
   }

   constexpr inline T& at_no_check(size_t i) const {
      return _data[i];
   }

   constexpr inline T& operator[](size_t i) const {
      return at(i);
   }
   constexpr inline T& operator[](size_t i) {
      return at(i);
   }
   constexpr inline T* raw() const {
      return _data;
   }
   constexpr inline T* data() const {
      return _data;
   }
   constexpr inline size_t size() const {
      return _size;
   }
   constexpr inline size_t capacity() const {
      return _capacity;
   }
   constexpr inline void set(T* data, size_t size, size_t index = -1) {
      _size = size;
      _capacity = size;
      _data = data;
      _index = index == -1 ? size - 1 : index;
   }
   constexpr inline void copy(T* data, size_t size) {
      resize(size);
      std::copy_n(data, size, _data);
      _index = size - 1;
   }

 private:
   constexpr inline void reserve(size_t capacity) {
      auto* data = _allocator->template alloc<T>(capacity);
      if (_size != 0)
         std::move(_data, _data + _size, data);
      _data = data;
      _capacity = capacity;
   }

   size_t _size = 0;
   size_t _capacity = 0;
   Allocator* _allocator = nullptr;
   T* _data = nullptr;
   size_t _index = 0;
};

struct unmanaged_base_member {
   using allocator = contiguous_allocator;
   unmanaged_base_member(size_t sz) : alloc(sz) {}
   allocator alloc;
};
} // namespace detail

template <typename T, typename Allocator> class managed_vector : public detail::vector<T, Allocator> {
 public:
   using detail::vector<T, Allocator>::vector;
   constexpr inline void set_owner(Allocator& alloc) {
      detail::vector<T, Allocator>::_allocator = &alloc;
   }
};

template <typename T> using unmanaged_vector = std::vector<T>;

template <typename T> std::string vector_to_string(T&& vec) {
   std::string str;
   str.resize(vec.size());
   for (std::size_t i = 0; i < vec.size(); ++i)
      str[i] = vec[i];
   return str;
}
} // namespace forge::vm::wasm
