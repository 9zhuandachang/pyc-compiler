# PyC Compiler & Virtual Machine

一个基于 C11 实现的 **PyC（Python + C 混合语法 / C-Minus 扩展风格）完整编译器与栈式字节码虚拟机**。

项目实现了从源代码到执行的完整编译流水线：

```text
Source (.pyc)
   │
   ▼  [1] Lexer (scanner.c)         ── 词法分析：行列号、逻辑行缩进、C/Python 注释与字面量
TokenList
   │
   ▼  [2] Parser (parser.c, ast.c)  ── 递归下降语法分析：花括号 {} 与冒号缩进 : 自由混写
  AST (TreeNode)
   │
   ▼  [3] Semantic Analyzer         ── 链式哈希作用域符号表 + 完整类型/控制流/左值检查
Typed AST + SymbolTable
   │
   ▼  [4] Bytecode Compiler         ── 栈式字节码生成、常量池、循环跳转回填、反汇编输出
BytecodeProgram
   │
   ▼  [5] Stack VM (vm.c)           ── 虚拟机执行、调用栈帧、堆数组/字符串、运行时越界与除零保护
Program Output
```

---

## 目录结构

```text
.
├── CMakeLists.txt           # CMake 构建与 CTest 测试配置
├── include/                 # 公共头文件
│   ├── common.h             # 内存分配辅助、文件读取与统一诊断列表
│   ├── scanner.h            # Token 定义与词法分析接口
│   ├── ast.h                # AST 节点与类型定义
│   ├── parser.h             # 语法分析接口
│   ├── symbol_table.h       # 链式哈希作用域符号表
│   ├── analyzer.h           # 语义分析与类型检查器
│   ├── bytecode.h           # 字节码指令集、常量池与反汇编接口
│   └── vm.h                 # 栈式虚拟机接口
├── src/                     # 核心源码实现与 CLI 入口
│   ├── common.c
│   ├── scanner.c
│   ├── ast.c
│   ├── parser.c
│   ├── symbol_table.c
│   ├── analyzer.c
│   ├── bytecode.c
│   ├── vm.c
│   └── main.c
├── examples/                # 示例程序与保留的原课程测试文件
│   ├── hybrid_showcase.pyc  # C/Python 混合语法综合演示
│   ├── fibonacci.pyc        # 递归与迭代斐波那契数列
│   ├── quicksort.pyc        # 原地快速排序（数组引用传参）
│   ├── legacy_pyc.txt       # 原仓库词法/语法测试样例
│   └── legacy_testP.txt     # 原仓库语法测试样例
└── tests/
    └── test_runner.c        # 覆盖词法、语法、语义、执行与运行时错误的自动化测试套件
```

---

## PyC 语言特性

- **双风格代码块自由混写**
  - C 风格花括号 `{ ... }`（块内不强制缩进）
  - Python 风格冒号 + 缩进 `: \n <indented block>`
  - 支持混合写法（如 `while x > y :\n { ... }` 或在 `{}` 块内嵌套 Python 缩进 `if`）
  - 语句末尾分号 `;` 可选，支持 `//`、`/* ... */`、`#` 三种注释
- **基础类型与数组**
  - 支持 `int`、`float`、`string`（别名 `str`/`char`）、`bool`、`void` 及一维数组 `T arr[N]`
  - 支持整数到浮点数的自动类型提升（`int -> float`）、字符串 `+` 拼接
- **双风格函数定义与互相递归**
  - C 风格：`int add(int a, int b) { return a + b; }`
  - Python 风格：`def add(a: int, b: int) -> int:`（省略 `-> type` 时自动从 `return` 语句推导返回类型）
  - 支持前向调用与互相递归；若定义了无参 `main()` 函数，程序会在初始化全局变量后自动调用 `main()`
- **完整控制流与运算符**
  - 控制流：`if / elif / else`、`while`、`do ... while`、`for`、`break`、`continue`、`return`、`pass`
  - 运算符：`+ - * / %`、`++ --`（前缀与后缀）、`= += -= *= /= %=`、`== != < <= > >=`、`&& || !`（别名 `and or not`，支持短路求值）
- **内置函数**
  - `print(...)` / `println(...)`：可变参数格式化输出
  - `write(x)`：单值换行输出（兼容 C-Minus）
  - `read()` / `read_int()` / `read_float()`：从标准输入读取整数或浮点数
  - `len(x)`：返回数组长度或字符串长度

---

## 构建与运行

### 使用 CMake 构建

**Windows (MSVC Developer Command Prompt / VS Build Tools):**

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

**Linux / macOS (GCC / Clang):**

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### CLI 用法

```text
Usage: pyc [options] <source-file>

Options:
  --tokens     打印词法 Token 流
  --ast        打印抽象语法树（AST）
  --symtab     打印分作用域符号表
  --bytecode   反汇编生成的字节码指令
  --trace      跟踪虚拟机每步指令执行
  --check      仅执行前端检查（配合 --bytecode 可只编译反汇编而不运行）
  -h, --help   显示帮助信息
```

### 运行示例

```powershell
# 直接编译并运行示例程序
.\build\pyc.exe examples\hybrid_showcase.pyc
.\build\pyc.exe examples\fibonacci.pyc
.\build\pyc.exe examples\quicksort.pyc

# 查看某程序的符号表与字节码反汇编（不执行）
.\build\pyc.exe --symtab --bytecode --check examples\hybrid_showcase.pyc
```
