// Copyright (c) 2020 smithemail@163.com. All rights reserved.
// Author：smithemail@163.com
// Time：2020-08-11

#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <gtest/gtest.h>

#include "hot_config.h"


class MyConfig {
  public:
    bool initilization(bool flag) {
        std::cerr << "MyConfig initilization processing" << std::endl;
        return flag;
    }

    bool loadValue(const std::string &config_path) {
        std::cerr << "MyConfig loadValue processing" << std::endl;
        std::ifstream ifs(config_path);
        std::string line;
        if (ifs.is_open()) {
            while (std::getline(ifs, line)) {
                vm.emplace(line, 1);
            }
            std::cout << "vm.size()=" << vm.size() << std::endl;
        } else {
            return false;
        }
        return true;
    }

  public:
    std::map<std::string, size_t> vm;
};


class HotConfigTest: public testing::Test {
  public:
    virtual void SetUp() {}
    virtual void TearDown() {}
};


TEST_F(HotConfigTest, FileConfig) {
    system("seq 8 > /tmp/1.txt");
    {
        HotConfig::FileConfig<MyConfig> my_config;
        ASSERT_EQ(my_config.setInit(&MyConfig::initilization, true), true);
        ASSERT_EQ(my_config.setLoad(&MyConfig::loadValue, "/tmp/1.txt"), true);
        ASSERT_EQ(my_config.load(), true);
        auto vp = my_config.get();
        ASSERT_EQ(vp->vm.size(), 8);
    }
    {
        HotConfig::FileConfig<std::vector<std::string>> my_config;
        ASSERT_EQ(my_config.setLoad([](std::vector<std::string> *c, int a) {
                        std::cerr << "init by setLoad func" << std::endl;
                        std::ifstream ifs("/tmp/1.txt");
                        std::string line;
                        if (ifs.is_open()) {
                            while (std::getline(ifs, line)) {
                                c->push_back(std::move(line));
                            }
                        }
                        return true;
                    }, 1), true);
        ASSERT_EQ(my_config.load(), true);
        auto vp = my_config.get();
        ASSERT_EQ(vp->size(), 8);
    }
    {
        HotConfig::FileConfig<std::vector<std::string>> my_config(
                [](std::vector<std::string> *c) {return true;},
                [](std::vector<std::string> *c) {
                    std::cerr << "init by instruction func" << std::endl;
                    std::ifstream ifs("/tmp/1.txt");
                    std::string line;
                    if (ifs.is_open()) {
                        while (std::getline(ifs, line)) {
                            c->push_back(std::move(line));
                        }
                    }
                    std::cerr << c->size() << std::endl;
                    return true;
                });
        ASSERT_EQ(my_config.load(), true);
        auto vp = my_config.get();
        ASSERT_EQ(vp->size(), 8);
    }
}


TEST_F(HotConfigTest, HotConfigManager) {
    HotConfig::HotConfigManager hotcmanager;
    {
        system("seq 8 > /tmp/1.txt");
        auto file_config = std::make_shared<HotConfig::FileConfig<MyConfig>>();
        ASSERT_EQ(file_config->setInit(&MyConfig::initilization, true), true);
        ASSERT_EQ(file_config->setLoad(&MyConfig::loadValue, "/tmp/1.txt"), true);
        ASSERT_EQ(file_config->setWatch("/tmp/1.txt"), true);
        ASSERT_EQ(file_config->load(), true);
        std::cerr << file_config->get() << std::endl;
        std::cerr << "11111, fd:" << file_config->getPassive() << std::endl;
        std::string key = "my_config_key";
        ASSERT_EQ(hotcmanager.add(key, file_config), true);
        hotcmanager.start();
        std::shared_ptr<MyConfig> vp;
        vp = hotcmanager.get<HotConfig::FileConfig, MyConfig>(key);
        std::cerr << vp.get() << std::endl;
        ASSERT_EQ(vp != nullptr, true);
        ASSERT_EQ(vp->vm.size(), 8);
        sleep(5);
        system("seq 8 16 >> /tmp/1.txt");
        sleep(10);
        auto p = hotcmanager.get<HotConfig::FileConfig, MyConfig>(key);
        std::cerr << p.get() << std::endl;
        ASSERT_EQ(p.get()->vm.size(), 16);
    }
    {
        std::cerr << "111111111111111111111111111111\n";
        system("seq 8 > /tmp/1.txt");
        std::string key = "my_config";
        auto file_config = std::make_shared<HotConfig::FileConfig<MyConfig>>();
        ASSERT_EQ(file_config->setInit(&MyConfig::initilization, true), true);
        ASSERT_EQ(file_config->setLoad(&MyConfig::loadValue, "/tmp/1.txt"), true);
        ASSERT_EQ(file_config->setWatch("/tmp/1.txt"), true);
        ASSERT_EQ(file_config->load(), true);
        ASSERT_EQ(file_config->setPassive(), true);
        hotcmanager.start();
        ASSERT_EQ(hotcmanager.add(key, file_config, true), true);
        auto p = hotcmanager.get<HotConfig::FileConfig, MyConfig>(key);
        std::cerr << p.get() << std::endl;
        ASSERT_EQ(p.get()->vm.size(), 8);
        system("seq 8 16 >> /tmp/1.txt; sleep 1");
        p = hotcmanager.get<HotConfig::FileConfig, MyConfig>(key);
        std::cerr << p.get() << std::endl;
        ASSERT_EQ(p.get()->vm.size(), 16);
    }
}


int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
