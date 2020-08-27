# HotConfig
mange configure hot reload

## 背景
写代码中经常碰见有自动加载更新配置的需求,但可能又不想重启服务, 这个时候就需要热加载的支持,
为了复用代码, 现在专门抽象出管理配置的逻辑,实现高效编码

## 思路
1. 使用一个统一的热配置管理器管理所有需要热更新的配置: HotConfig::HotConfigManager
    * 支持主动定时轮询检测
    * 支持被动触发更新(inotify+epoll)
2. 现阶段对于具体配置, 常见为文件热更新, 使用 HotConfig::FileConfig包装具体的配置对象
3. 基于c++11
4. 纯头文件,方便嵌入项目中使用

## 功能
* 使用智能指针管理配置对象, 实时从热配置管理器获取最新配置并保证数据安全无内存泄漏风险
* 线程安全
* 基于模板的实现使之能够支持任意的数据对象
* **主动**轮询检查是否更新配置/  **被动**触发更新配置

## 使用

1. 初始化一个管理器对象
2. 使用FileConfig包装你的具体对象, 如: HotConfig::FileConfig<MyConfigObject>
3. 设置对象的属性:
    * 对象初始函数调用: std::function<bool(MyConfigObject*)>
    * 对象加载函数调用: std::function<bool(MyConfigObject*)>
    * 监听文件路径及其检测函数
    * 是否被动出发更新(默认关闭触发更新)
4. 将FileConfig对象放入管理器中


例子:
```
int main() {
    HotConfig::HotConfigManager hotcmanager;  // 初始化管理器
    hotcmanager.start();
    const std::string fname = "/tmp/1.txt";
    const std::string key = "myconfigUniqName";
    system("seq 8 > /tmp/1.txt");
    // 创建一个从文件加载配置的对象std::vector<std::string>
    // 定义了init函数和load函数, 使用智能指针管理
    auto config = std::make_shared<HotConfig::FileConfig<std::vector<std::string>>(
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
    // 设置监听对象, 当该文件更新时,更新配置
    config->setWatch(fname);
    // 将HotConfig::FileConfig指针加入到热更新管理器中
    hotcmanager.add(key, config);
    // 通过key 获取到最新的std::vector<std::string>配置指针
    auto vp = hotcmanager.get<HotConfig::FileConfig, std::vector<std::string>>(key);

    auto pconfig = std::make_shared<HotConfig::FileConfig<std::vector<std::string>>(
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
    pconfig->setWatch(fname);
    pconfig->setPassive();// 设置被动触发更新该配置
    hotcmanager.add("passiveConfig", pconfig);
    vp = hotcmanager.get<HotConfig::FileConfig, std::vector<std::string>>("passiveConfig");
}

```

## 计划
1. 单线程检测, 线程池实际加载  :heavy_check_mark:
2. 支持事件驱动更新:heavy_check_mark:
3. 支持外部手动触发调度更新
4. 支持多文件watch, 同时更新出发reload或任意更新触发reload
