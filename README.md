基于Linux的轻量型多线程HTTP服务器
===============

在Linux环境下使用C++语言开发的轻量型多线程Web服务器， 在应用层实现了一个简单的HTTP服务器， 支持用户验证、静态资源访问应答、设置定时器自动断 联长时间无请求的连接以及服务器记录日志等功能

* 使用 **线程池 + 非阻塞socket + epoll+ 事件处理(模拟Proactor模式均实现)** 的并发模型
* 使用**主从状态机**解析HTTP请求报文，支持解析**GET和POST**请求各类资源，包括html、js、css、图片、视频等
* 访问服务器数据库实现web端用户**注册、登录**功能，可以请求服务器**图片和视频文件**
* 实现**同步/异步日志系统**，记录服务器运行状态
* 经Webbench压力测试可以实现**上万的并发连接**数据交换

Demo演示
----------

> * 网站首页——可正常解析html、js、css文件，配有登陆和注册两个选项

![image-20230312224043228](https://sugar-pictures.oss-cn-shanghai.aliyuncs.com/202303122240343.png)

> * 注册演示

注册页面

![image-20230312224657144](https://sugar-pictures.oss-cn-shanghai.aliyuncs.com/202303122246167.png)

名字冲突提醒

![image-20230312224735622](https://sugar-pictures.oss-cn-shanghai.aliyuncs.com/202303122247651.png)

> * 登录演示

登陆页面

![image-20230312224159078](https://sugar-pictures.oss-cn-shanghai.aliyuncs.com/202303122241121.png)

输入登录![image-20230312224248834](https://sugar-pictures.oss-cn-shanghai.aliyuncs.com/202303122242865.png)

错误提醒

![image-20230312224312112](https://sugar-pictures.oss-cn-shanghai.aliyuncs.com/202303122243144.png)

登陆成功

![image-20230312224334707](https://sugar-pictures.oss-cn-shanghai.aliyuncs.com/202303122243728.png)

> * 请求图片文件演示——三张，共3MB

![image-20230312224439026](https://sugar-pictures.oss-cn-shanghai.aliyuncs.com/202303122244066.png)

> * 请求视频文件演示（20.4MB）

![image-20230312224539351](https://sugar-pictures.oss-cn-shanghai.aliyuncs.com/202303122245402.png)


压力测试
-------------

打开日志，使用Webbench对服务器进行压力测试，对listenfd和connfd采用LT模式，可实现上万的并发连接，下面是测试结果. 

![image-20230312230728536](https://sugar-pictures.oss-cn-shanghai.aliyuncs.com/202303122307561.png)

并发连接总数：25000

> * 访问服务器时间：5s
> * 所有访问均成功
> * QPS：80392.4

**注意：** 使用本项目的webbench进行压测时，若报错显示webbench命令找不到，将可执行文件webbench删除后，重新编译即可。

更新日志
-------

- [x] 使用RAII机制优化数据库连接的获取与释放
- [x] 增加数据库连接池与CGI用户校验机制
- [x] 用户密码MD5加密🔐
- [x] 增加对于JS、CSS文件类型的支持
- [x] 增加请求视频文件的页面
- [x] 引入Webbench模块完成压力测试
- [x] 改进代码结构，更新局部变量懒汉单例模式

- [x] main函数封装重构
- [x] 改进编译方式，增加MySQL配置文件，只需配置一次SQL信息

目录树
------------

```
.
├── Connection_pool 数据库连接池        
├── http            http连接类
├── locker          Linux下互斥锁、信号量、条件变量封装
├── log             日志模块
├── md5             MD5加密模块
├── threadpool      线程池
├── timer           定时器
├── test_presure    webbench压力测试
│
├── webserver       静态资源
│   ├── source      媒体资源
│   │   ├── pictures
│   │   └── videos     
│   ├── style      
│   │    ├── js
│   │    └── css
│   ├── index.html
│   ├── log.html
│   ├── logError.html
│   ├── register.html
│   ├── resiterError.html
│   ├── welcome.html
│   ├── picture.html
│   ├── video.html
│   └── fans.html
│
├── main.cpp        主程序
├── build           数据库连接池配置文件 
├── makefile
├── GET报文.txt
└── README.md
```

快速运行
------------

* 服务器测试环境

  * Ubuntu 版本20.04.2
  * MySQL  版本8.0.32

* 浏览器测试环境

  * Mac、Windows、Linux均可
  * Chrome、Edge、FireFox均可
  * 其他浏览器暂无测试

* 测试前确认已安装MySQL数据库

  ```C++
  // 确保已经简历WebServer数据库
  USE WebServer;
  
  // 建立用户表
  CREATE TABLE IF NOT EXISTS `user_info`(
     `id` INT(11) UNSIGNED AUTO_INCREMENT,
     `name` VARCHAR(50) DEFAULT NULL,
     `password` VARCHAR(200) NOT NULL,
     `age` INT(11) DEFAULT NULL,
     `sex` ENUM('male', 'female', 'privary'),
     PRIMARY KEY (`id`)
  ) ENGINE = InnoDB DEFAULT CHARSET = utf8;
  
  // 插入两条测试数据
  INSERT INTO user_info (name, password, age, sex) VALUES ('Tom', 'test_password', 10, 'male');
  INSERT INTO user_info (name, password, age, sex) VALUES ('Amy', 'test_password2', 11, 'female');
  ```

* 更新mysql.conf中的数据库初始化信息

  ```C++
  # 数据库连接池的配置文件
  ip=localhost
  port=3306
  username=sugar
  password=test
  dbname=WebServer
  initSize=10
  maxSize=1024
  # 默认最大空闲时间是秒
  maxIdleTime=30
  # 默认连接超时单位是毫秒
  connectionTimeout=100
  ```

* build

  ```bash
  make server
  ```

* 清理

  ```bash
  make clean
  ```

* 启动server

  ```bash
  ./server 8001
  ```

* 浏览器端

  ```http
  ip:8001
  ```