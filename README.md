# HotConfig
mange configure hot reload

## 使用

直接将头文件拷贝过去即可使用, 例子:
```
int main() {
    HotConfig::HotConfigManager hotcmanager;  // 初始化管理器
    const std::string fname = "/tmp/1.txt";
    const std::string key = "my_config_key";
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
    config->setWatch(fname), true);
    // 将HotConfig::FileConfig指针加入到热更新管理器中
    hotcmanager.add(key, config);
    hotcmanager.start();
    // 通过key 获取到最新的std::vector<std::string>配置指针
    vp = hotcmanager.get<HotConfig::FileConfig, std::vector<std::string>>(key);
}

```
