// Copyright (c) 2020 smithemail@163.com. All rights reserved.
// Author：smithemail@163.com
// Time：2020-08-11

#ifndef HOT_CONFIG_H_ 
#define HOT_CONFIG_H_ 

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <set>
#include <map>
#include <atomic>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>


namespace HotConfig {
    
class BaseConfig {
  public:
    BaseConfig() {}
    ~BaseConfig() {}
    
    virtual bool load() = 0;
    
    virtual bool needUpdate() = 0;
};


template<typename T>
class FileConfig : public BaseConfig {
  public:
    FileConfig() {}
    FileConfig(std::function<bool(T*)> &&init_func,
               std::function<bool(T*)> &&load_func)
            : init_func_(init_func), load_func_(load_func) {}
    ~FileConfig() {}

    template<typename F, typename... Args>
    bool setInit(F &&f, Args&&... args) {
        init_func_ = std::bind(std::forward<F>(f),
                               std::placeholders::_1,
                               std::forward<Args>(args)...);
        return true;
    }
    
    template<typename F, typename... Args>
    bool setLoad(F &&f, Args&&... args) {
        load_func_ = std::bind(std::forward<F>(f),
                               std::placeholders::_1,
                               std::forward<Args>(args)...);
        return true;
    }

    bool setWatch(const std::string &file_path,
                  const std::function<bool(const std::string &,
                      std::string &)> &&func_detect = fileMTimeUpdated) {
        if (file_path.empty()) {
            return false;
        }
        if (!file_map_.count(file_path)) {
            file_map_[file_path] = "";
            file_detects_.emplace(file_path, func_detect);
        }
        return true;
    }
    
    bool load() {
        auto new_config = std::make_shared<T>();
        if (init_func_ && !init_func_(new_config.get())) {
            return false;
        }
        if (load_func_ && !load_func_(new_config.get())) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        config_ptr_.swap(new_config);
        return true;
    }
    
    bool needUpdate() {
        for (auto &p: file_map_) {
            if (!file_detects_.count(p.first)) {
                continue;
            }
            auto &&func_detect = file_detects_[p.first];
            if (func_detect(p.first, p.second)) {
                return true;
            }
        }
        return false;
    }

    std::shared_ptr<T> get() {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_ptr_;
    }

    static bool fileMTimeUpdated(const std::string &file_path,
                                 std::string &mtime) {
        if (file_path.empty()) {
            return false;
        }
        struct stat fstat;
        if (stat(file_path.c_str(), &fstat)) {
            return false;
        }
        std::string new_mtime = std::to_string(fstat.st_mtime);
        if (mtime == new_mtime) {
            return false;
        }
        std::cerr << "new time:" << new_mtime << ", old time:" << mtime << std::endl;
        mtime = new_mtime;
        return true;
    }

  public:
    std::mutex mutex_;
    std::map<std::string, std::string> file_map_;
    std::map<std::string,
             std::function<bool(const std::string&, std::string &)>> file_detects_;
    std::shared_ptr<T> config_ptr_;
    std::function<bool(T*)> init_func_;
    std::function<bool(T*)> load_func_;
};


class HotConfigManager {
  public:
    HotConfigManager() : check_interval_(10), running_(false) {}
    ~HotConfigManager() {
        stop();
    }

    bool start() {
         running_ = true;
         reload_thread_ = std::make_shared<std::thread>(&HotConfigManager::cycleReload,
                                                        this);
         return true;
    }

    bool stop() {
        if (running_) {
            running_ = false;
            exit_cond_.notify_all();
            reload_thread_->join();
        }
        return true;
    }
    
    template<typename T>
    bool add(const std::string &key,
             std::shared_ptr<T> config,
             const bool recover=true) {
        if (key.empty() || !config) {
            return false;
        }
        if (config_map_.count(key) && !recover) {
            return false;
        }
        if (config_map_.count(key)) {
            size_t idx = config_map_.find(key)->second;
            if (idx >= config_pool_.size()) {
                return false;
            }
            auto cfg = std::dynamic_pointer_cast<BaseConfig>(config);
            config_pool_[idx].swap(cfg);
        } else {
            config_pool_.push_back(config);
            config_map_[key] = config_pool_.size()-1;
        }
        return true;
    }
    
    template<template<typename R> class T, typename R>
    std::shared_ptr<R> get(const std::string &key) {
        if (!config_map_.count(key)) {
            return std::shared_ptr<R>();
        }
        auto p = std::dynamic_pointer_cast<T<R>>(config_pool_[config_map_[key]]);
        return p->get();
    }
    
    bool loadAll();

  public:
    bool cycleReload() {
        while (running_) {
            std::cerr << "runing cycleReload" << std::endl;
            for (size_t i = 0; i < config_pool_.size(); ++i) {
                auto config_ptr = config_pool_[i];
                if (running_ && config_ptr->needUpdate()) {
                    std::cerr << "reload config:" << i << std::endl;
                    config_ptr->load();
                }
            }
            if (!running_) {
                break;
            }
            std::unique_lock<std::mutex> locker(exit_mutex_);
            auto now = std::chrono::system_clock::now();
            exit_cond_.wait_until(locker, now+std::chrono::seconds(check_interval_));
        }
        return true;
    }

  private:
    size_t check_interval_;
    std::atomic<bool> running_;
    std::shared_ptr<std::thread> reload_thread_;
    std::map<std::string, size_t> config_map_;
    std::vector<std::shared_ptr<BaseConfig>> config_pool_;
    std::mutex exit_mutex_;
    std::condition_variable exit_cond_;
};

}

#endif 
