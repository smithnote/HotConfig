// Copyright (c) 2020 smithemail@163.com. All rights reserved.
// Author：smithemail@163.com
// Time：2020-08-11

#ifndef HOT_CONFIG_H_ 
#define HOT_CONFIG_H_ 

#include <error.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <atomic>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "thread_pool.h"

namespace HotConfig {
    
class BaseConfig {
  public:
    BaseConfig() {}
    ~BaseConfig() {}
    
    virtual bool load() = 0;
    
    virtual bool needUpdate() = 0;

    virtual int getPassive() {
        return -1;
    };
};


template<typename T>
class FileConfig : public BaseConfig {
  public:
    FileConfig() : fd_(-1) {}
    FileConfig(std::function<bool(T*)> &&init_func,
               std::function<bool(T*)> &&load_func)
             : fd_(-1), init_func_(init_func), load_func_(load_func) { }
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

    bool setPassive(const std::function<bool(int&,
                const std::map<std::string, std::string>)> &&func = fileInotify) {
        return func(fd_, file_map_);
    }
    
    virtual bool load() {
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
    
    virtual bool needUpdate() {
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

    virtual int getPassive() {
        return fd_;
    }

    std::shared_ptr<T> get() {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_ptr_;
    }

  public:
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
        mtime = new_mtime;
        return true;
    }

    static bool fileInotify(int &fd, const std::map<std::string, std::string> &files) {
        fd = inotify_init();
        if (fd == -1) {
            return false;
        }
        for (auto &p: files) {
            int ret = inotify_add_watch(fd, p.first.c_str(), IN_CREATE|IN_MODIFY);
            if (ret == -1) {
                fd = -1;
                return false;
            }
        }
        return true;
    }

  private:
    int fd_;
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
    HotConfigManager(const HotConfigManager&) = delete;
    HotConfigManager& operator=(const HotConfigManager&) = delete;
    ~HotConfigManager() {
        stop();
    }

    bool start() {
        if (running_) {
            return true;
        }
        running_ = true;
        passive_thread_.reset(new std::thread(&HotConfigManager::passiveReload, this));
        cycle_thread_.reset(new std::thread(&HotConfigManager::cycleReload, this));
        reload_pool_ = std::make_shared<HotConfig::ThreadPool>();
        reload_pool_->start();
        return true;
    }

    bool stop() {
        if (running_) {
            running_ = false;
            exit_cond_.notify_all();
            cycle_thread_->join();
            write(stop_pipe_[1], "a", 1);
            passive_thread_->join();
            reload_pool_->stop();
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
        if (key_map_.count(key) && !recover) {
            return false;
        }
        std::unique_lock<std::mutex> lock(config_mutex_);
        if (key_map_.count(key)) {
            size_t idx = key_map_.find(key)->second;
            if (idx >= config_pool_.size()) {
                return false;
            }
            auto cfg = std::dynamic_pointer_cast<BaseConfig>(config);
            config_pool_[idx].swap(cfg);
        } else {
            config_pool_.push_back(config);
            key_map_[key] = config_pool_.size()-1;
        }
        int fd = config->getPassive();
        if (fd != -1) {
            addPassive(fd);
            fd_map_[fd] = key_map_[key];
        }
        return true;
    }
    
    template<template<typename R> class T, typename R>
    std::shared_ptr<R> get(const std::string &key) {
        if (!key_map_.count(key)) {
            return std::shared_ptr<R>();
        }
        std::unique_lock<std::mutex> lock(config_mutex_);
        auto p = std::dynamic_pointer_cast<T<R>>(config_pool_[key_map_[key]]);
        lock.unlock();
        return p->get();
    }
    
    bool loadAll() {
        for (size_t i = 0; i < config_pool_.size(); ++i) {
            auto config_ptr = config_pool_[i];
            reload_pool_->push([config_ptr](){config_ptr->load();});
        }
        return true;
    }

  private:
    bool cycleReload() {
        while (running_) {
            for (size_t i = 0; i < config_pool_.size(); ++i) {
                auto config_ptr = config_pool_[i];
                if (config_ptr->getPassive() != -1) {
                    continue;
                }
                if (running_ && config_ptr->needUpdate()) {
                    reload_pool_->push([config_ptr](){config_ptr->load();});
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

    bool passiveReload() {
        epoll_fd_ = epoll_create(1);
        if (epoll_fd_ == -1) {
            return false;
        }
        if (pipe(stop_pipe_) == -1 || !addPassive(stop_pipe_[0])) {
            return false;
        }
        char buffer[1024];
        struct epoll_event events[10];
        while (true) {
            int nfds = epoll_wait(epoll_fd_, events, 10, -1);
            if (nfds < 0 && errno == EINTR) {
                continue;
            }
            if (nfds < 0) {
                return false;
            }
            for (int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                if (fd == stop_pipe_[0]) {
                    return true;
                }
                if (!fd_map_.count(fd)) {
                    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &events[i]);
                } else {
                    read(fd, (void*)buffer, 1024);
                    auto config_ptr = config_pool_[fd_map_[fd]];
                    reload_pool_->push([config_ptr](){config_ptr->load();});
                }
            }
        }
        return true;
    }

    bool addPassive(int fd) {
        if (fd == -1) {
            return false;
        }
        struct epoll_event pevent;
        pevent.data.fd = fd;
        pevent.events = EPOLLIN | EPOLLET;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &pevent);
        return true;
    }

  private:
    int epoll_fd_;
    int stop_pipe_[2];
    size_t check_interval_;
    std::atomic<bool> running_;
    std::shared_ptr<std::thread> passive_thread_;
    std::shared_ptr<std::thread> cycle_thread_;
    std::map<std::string, size_t> key_map_;
    std::map<int, size_t> fd_map_;
    std::vector<std::shared_ptr<BaseConfig>> config_pool_;
    std::mutex config_mutex_;
    std::mutex exit_mutex_;
    std::condition_variable exit_cond_;
    std::shared_ptr<HotConfig::ThreadPool> reload_pool_;
};

}

#endif 
