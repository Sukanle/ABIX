# ABI 身份与兼容性

ABIX 把经常被混用的几种身份区分开来，使"是不是同一个类型"和"这个二进制能不能安全使用"
成为精确的问题。

## 四种身份

| ID | 含义 | 回答的问题 |
|----|------|-----------|
| **TypeID** | 类型语义身份（128 位） | 是不是同一个类型 |
| **LayoutHash** | 物理布局身份 | 内存布局是否兼容 |
| **BuildID** | 二进制构建身份 | 该 artifact 属于哪个二进制 |
| **MetadataID** | Metadata 内容身份 | metadata image 是否完整 |

查找链条：

```text
Binary → BuildID → .abix → MetadataID → TypeID → LayoutHash
```

* `TypeID` 与名字无关：两个模块可以用不同拼写表示同一类型而共享 TypeID；名字只用于诊断。
* `LayoutHash` 覆盖 size、alignment、字段偏移/类型——真正决定调用是否安全的部分。
* `BuildID` 来自 ELF `.note.gnu.build-id`（或由 AMC 注入），用于索引 symbol server。
* `MetadataID` 是 region 内容（desc + hash + names）的 hash，用于检测篡改。

## 兼容性分类

`amc compatibility` 与 `amc query --compatible` 对每个类型分类：

| 类型 | 含义 |
|------|------|
| `identical` | TypeID 与 LayoutHash 都相同 |
| `layout_compatible` | 实际布局兼容 |
| `map_compatible` | 布局不同但存在映射 |
| `incompatible` | 无 adapter 时不能安全使用 |

`amc verify` 更严格：**任何**差异（包括新增类型/函数）都算 drift，因为扩大 ABI surface
必须是一个显式决策。

## 加载期跨 Module 校验

`RuntimeRegistry` 注册模块时，同一 TypeID 必须解析到同一 LayoutHash：

```text
TypeID 同时出现在 ModuleA / ModuleB
        ├── 布局相同 → 兼容，去重共享
        └── 布局不同 → layout_conflict（ABI 冲突）
```

兼容的重复会被共享，冲突的布局会被拒绝。这是 ABI 边界在运行时的实现。

## Source Origin

为支持 go-to-definition，`.abix` 可携带可选、仅 debug 的 `sources` 段，映射
`TypeID → file:line:column`。它**不参与 `abi_hash`**——声明位置是诊断信息，不是 ABI 身份。
