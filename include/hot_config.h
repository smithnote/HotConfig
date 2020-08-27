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
#include <sys/ioctl.h>
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

#define SETBIT(x, y) x|=(1<<y)
#define CLRBIT(x, y) x&=~(1<<y)
#define BSETBIT(x, y) x=(~(~(0)<<y))


namespace HotConfig {
    
class BaseConfig {
  public:
    BaseConfig() {}
    ~BaseConfig() {}
    
    virtual bool load() = 0;
    
    virtual bool needUpdate() = 0;

    virtual int getPassive() {return -1;}
    
    virtual bool passiveUpdate(int) { return true;}
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
        return setWatch(std::vector<std::string>({file_path}),
                        std::forward<decltype(func_detect)>(func_detect));
    }
    
    bool setWatch(const std::vector<std::string> &file_vec,
                  const std::function<bool(const std::string &,
                      std::string &)> &&func_detect = fileMTimeUpdated) {
        if (file_vec.empty()) {
            return false;
        }
        if (file_vec.size() > 32) {
            return false;
        }
        for (const std::string &file: file_vec) {
            if (file.empty()) {
                return false;
            }
        }
        status_vec_.push_back(0);
        files_vec_.push_back({});
        for (int i = 0; i < file_vec.size(); ++i) {
            auto &file = file_vec[i];
            file_map_[file] = "";
            files_vec_.back().emplace_back(file);
            file_detects_.emplace(file, func_detect);
            SETBIT(status_vec_.back(), i);
        }
        return true;
    }

    bool setPassive(const std::function<bool(const std::map<std::string, std::string> &,
                                             std::map<int, std::string>&,
                                             int&)> &&func = fileInotify,
                    const std::function<bool(int, const std::map<int, std::string>&,
                        std::set<std::string>&)> &&cb = fileInotifyCallback) {
        passive_cb_ = std::move(cb);
        return func(file_map_, fd_map_, fd_);
    }
    
    virtual bool load() override {
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
    
    virtual bool needUpdate() override {
        for (int i = 0; i < files_vec_.size(); ++i) {
            const auto &file_vec = files_vec_[i];
            if (file_vec.empty()) {
                continue;
            }
            int fsize = file_vec.size();
            for (int j = 0; j < fsize; ++j) {
                const std::string &file = file_vec[j];
                if (!file_detects_.count(file)) {
                    continue;
                }
                auto &func_detect = file_detects_[file];
                if (func_detect(file, file_map_[file])) {
                    CLRBIT(status_vec_[i], j);
                }
            }
            if (!status_vec_[i]) {
                BSETBIT(status_vec_[i], fsize);
                return true;
            }
        }
        return false;
    }

    virtual int getPassive() override {
        return fd_;
    }

    virtual bool passiveUpdate(int fd) override {
        std::set<std::string> update_set;
        if (!passive_cb_(fd, fd_map_, update_set)) {
            return false;
        }
        bool update = false;
        for (int i = 0; i < files_vec_.size(); ++i) {
            const auto &file_vec = files_vec_[i];
            if (file_vec.empty()) {
                continue;
            }
            int fsize = file_vec.size();
            for (int j = 0; j < fsize; ++j) {
                if (update_set.count(file_vec[j])) {
                    CLRBIT(status_vec_[i], j);
                }
            }
            if (!status_vec_[i]) {
                BSETBIT(status_vec_[i], fsize);
                update = true;
            }
        }
        return update;
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

    static bool fileInotify(const std::map<std::string, std::string> &files, 
                            std::map<int, std::string> &fd_map, int &fd) {
        fd = inotify_init();
        if (fd == -1) {
            return false;
        }
        fd_map.clear();
        for (auto &p: files) {
            int wd = inotify_add_watch(fd, p.first.c_str(), IN_CREATE|IN_MODIFY);
            if (wd == -1) {
                fd_map.clear();
                fd = -1;
                return false;
            }
            fd_map[wd] = p.first;
        }
        return true;
    }

    static bool fileInotifyCallback(int fd,
                                  const std::map<int, std::string> &fd_map,
                                  std::set<std::string> &update_files) {
       	unsigned int avail;
		ioctl(fd, FIONREAD, &avail);
		char *buffer = (char*)calloc(avail, sizeof(char));
		int len = read(fd, buffer, avail);
		int offset = 0;
        std::set<std::string> update_set;
		while (offset < len) {
		    struct inotify_event *event = (inotify_event*)(buffer + offset);
            if (fd_map.count(event->wd)) {
                update_files.insert(fd_map.find(event->wd)->second);
            }
		    offset = offset + sizeof(inotify_event) + event->len;
		}
        free(buffer);
        return true;
    }

  private:
    int fd_;
    std::mutex mutex_;
    std::vector<std::vector<std::string>> files_vec_;
    std::vector<int> status_vec_;
    std::map<std::string, std::string> file_map_;
    std::map<int, std::string> fd_map_;
    std::map<std::string,
             std::function<bool(const std::string&, std::string &)>> file_detects_;
    std::shared_ptr<T> config_ptr_;
    std::function<bool(T*)> init_func_;
    std::function<bool(T*)> load_func_;
    std::function<bool(int, const std::map<int, std::string>&,
                       std::set<std::string>&)> passive_cb_;
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
                    auto config_ptr = config_pool_[fd_map_[fd]];
                    if (config_ptr->passiveUpdate(fd)) {
                        reload_pool_->push([config_ptr](){config_ptr->load();});
                    }
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
