#pragma once
#include <map>
namespace berberis {
template<class K, class V, class C = std::less<K>> using ForeverMap = std::map<K, V, C>;
}
