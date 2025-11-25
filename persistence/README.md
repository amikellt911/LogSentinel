Mvp后优化点
1.“received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP”改为时间戳Unix的int，因为高负载下，网络接收到任务和任务执行的时间差可能会很长，到时候要onmessage或httpcallback的时候要获取当前时间戳。
2.和线程池优化结合，每一个work线程池都有一个各自的thread_local的sqlite3句柄，这样可以优化，通过NOMUTEX代替FULLMUTEX（flags，sqlite3_open_v2的选项）
3.如果SaveLog失败，抛出异常，异常处理的时候，可以加入重试或写入死信队列等机制。
4.如果出现SQLITE_BUSY，要进行处理，否则会直接抛异常，因为sqlite设计无法无限阻塞，
遇到的Bug：
1.测试时：“ASSERT_THROW((SqliteLogRepository(invalid_path)), std::runtime_error);”如果少加一个括号用“ASSERT_THROW(SqliteLogRepository(invalid_path), std::runtime_error);”，
另一个表达式方法，也是C++11后推荐的,使用SqliteLogRepository{invalid_path}花括号来初始化
原因是C++解析器在解析SqliteLogRepository(invalid_path)的时候，我们人类看来这就是一个临时对象并用invaild_path初始化的一个表达式，但是这只是一种情况，其实还有一种情况，即声明，声明一个变量，因为C++允许变量声明的时候用括号，比如
```cpp
    int(x) => int x;
    x=1;
```
这样其实就看出来了，编译器看到歧义，这种情况下，编译器有一条规则是如果代码可以解析为声明，那么它必须被解析为声明，因为可以被解析，所以只能走这条道路，那么就如同我们说的那样,SqliteLogRepository(invalid_path)=>SqliteLogRepository x;这样，但是SqliteLogRepository没有默认构造函数，所以就报了这个错误，但是你可以
```cpp
	这样他就被解析成了表达式了，一个Int的临时对象通过x初始化，但是x之前我们没有define
	(int(x));
	identifier "x" is undefined
```
就会报如下错误“[{
	"resource": "/home/llt/Project/llt/LogSentinel/tests/SqliteLogRepository_test.cpp",
	"owner": "C/C++: IntelliSense",
	"code": "291",
	"severity": 8,
	"message": "no default constructor exists for class \"SqliteLogRepository\"",
	"source": "C/C++",
	"startLineNumber": 107,
	"startColumn": 5,
	"endLineNumber": 107,
	"endColumn": 17
}]”