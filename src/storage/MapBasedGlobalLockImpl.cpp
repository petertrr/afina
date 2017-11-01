#include "MapBasedGlobalLockImpl.h"

#include <mutex>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value)
{
    std::unique_lock<std::mutex> guard(_lock);
    if( _backend.find(key) == _backend.end() )
    {
        if( _order.size() + 1 > _max_size ) {
            _backend.erase(_order.front());
            _order.pop_front();
        }
        _order.push_back(key);
    }
    _backend[key] = value;
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value)
{
    std::unique_lock<std::mutex> guard(_lock);
    //if( _backend.find(key) == _backend.end() )
    if( _backend.count(key) == 0 )
    {
        if( _order.size() + 1 > _max_size ) {
            _backend.erase(_order.front());
            _order.pop_front();
        }
        _order.push_back(key);
        _backend[key] = value;
        return true;
    //    return Put(key, value);
    }
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Set(const std::string &key, const std::string &value)
{
    std::unique_lock<std::mutex> guard(_lock);
    if( _backend.find(key) != _backend.end() )
    {
        _backend[key] = value;
        return true;
    }
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Delete(const std::string &key)
{
    if( _backend.find(key) != _backend.end() )
    {
        std::unique_lock<std::mutex> guard(_lock);
        _backend.erase(key);
        _order.remove(key);
        return true;
    }
    return false;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Get(const std::string &key, std::string &value) const
{
    std::unique_lock<std::mutex> guard(*const_cast<std::mutex *>(&_lock));
    if( _backend.find(key) != _backend.end() )
    {
        value = _backend.at(key);
        return true;
    }
    return false;
}

} // namespace Backend
} // namespace Afina
