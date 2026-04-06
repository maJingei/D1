#pragma once

#include "../Memory/Allocator.h"

#include <vector>
#include <map>
#include <set>
#include <string>

namespace D1
{
	template <typename T>
	using Vector = std::vector<T, StlAllocator<T>>;

	template <typename K, typename V>
	using Map = std::map<K, V, std::less<K>,
		StlAllocator<std::pair<const K, V>>>;

	template <typename T>
	using Set = std::set<T, std::less<T>, StlAllocator<T>>;

	using String = std::basic_string<char, std::char_traits<char>,
		StlAllocator<char>>;

	using WString = std::basic_string<wchar_t, std::char_traits<wchar_t>,
		StlAllocator<wchar_t>>;
}