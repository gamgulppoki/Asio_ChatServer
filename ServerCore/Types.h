#pragma once

#include <mutex>
#include <atomic>
#include <memory>
#include <array>
#include <vector>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <queue>
#include <stack>
#include <string>
#include <functional>
#include <asio.hpp>

// =============================================
// 기본 정수 타입
// =============================================
using BYTE = unsigned char;
using int8 = __int8;
using int16 = __int16;
using int32 = __int32;
using int64 = __int64;
using uint8 = unsigned __int8;
using uint16 = unsigned __int16;
using uint32 = unsigned __int32;
using uint64 = unsigned __int64;

// =============================================
// STL 타입 별칭
// =============================================
template<typename T, size_t N>
using Array = std::array<T, N>;

template<typename T>
using Vector = std::vector<T>;

template<typename T>
using Set = std::set<T>;

template<typename K, typename V>
using Map = std::map<K, V>;

template<typename T>
using HashSet = std::unordered_set<T>;

template<typename K, typename V>
using HashMap = std::unordered_map<K, V>;

template<typename T>
using Deque = std::deque<T>;

template<typename T>
using Queue = std::queue<T>;

template<typename T>
using Stack = std::stack<T>;

using String = std::string;
using WString = std::wstring;

template<typename T>
using Function = std::function<T>;

template<typename T>
using SharedPtr = std::shared_ptr<T>;

template<typename T>
using WeakPtr = std::weak_ptr<T>;

template<typename T>
using UniquePtr = std::unique_ptr<T>;

// =============================================
// 동기화 타입
// =============================================
template<typename T>
using Atomic = std::atomic<T>;
using Mutex = std::mutex;
using CondVar = std::condition_variable;
using UniqueLock = std::unique_lock<std::mutex>;
using LockGuard = std::lock_guard<std::mutex>;

// =============================================
// asio 타입 별칭
// =============================================
using IoContext = asio::io_context;
using TcpSocket = asio::ip::tcp::socket;
using TcpAcceptor = asio::ip::tcp::acceptor;
using TcpEndpoint = asio::ip::tcp::endpoint;
using TcpResolver = asio::ip::tcp::resolver;
using ErrorCode = std::error_code;

// =============================================
// shared_ptr 매크로
// =============================================
#define USING_SHARED_PTR(name) using name##Ref = std::shared_ptr<class name>;
