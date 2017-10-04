#include "MapBasedGlobalLockImpl.h"

#include <mutex>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::Put(const std::string &key, const std::string &value)
{
    std::unique_lock<std::mutex> guard(_lock);
    if( _order.size() + 1 > _max_size )
        _order.pop_front();
    _backend[key] = value;
    _order.push_back(key);
    return true;
}

// See MapBasedGlobalLockImpl.h
bool MapBasedGlobalLockImpl::PutIfAbsent(const std::string &key, const std::string &value)
{
    std::unique_lock<std::mutex> guard(_lock);
    if( _backend.find(key) == _backend.end() )
    {
        if( _order.size() + 1 > _max_size )
            _order.pop_front();
        _backend[key] = value;
        _order.push_back(key);
        return true;
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
        for(auto it = _order.begin(); it != _order.end(); ++it)
            if(*(it) == key) {
                _order.erase(it);
                break;
            }
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
