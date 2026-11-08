# Safety metadata（v0.3.x 决策）

<p align="center">
  中文 · <a href="safety.md">English</a>
</p>

> **状态：v0.3.x 决策。** Safety metadata 是 advisory 的。ABIX 只记录它存放在哪里、允许哪些声明；不在运行期强制 S1/S2。

## 元数据存放位置

- **AMC `.semantic`** 是 S1/S2 标注的权威文档。
- **ABIX `.abix`** 在 v0.3.x 不新增字段。reader 必须接受没有 safety metadata 的产物；标注不参与 `TypeID`、`LayoutHash` 或 `ABIHash`。
- **Runtime** 不强制 aliasing、`Send` 或 `Sync`。生成的语言 wrapper 可以使用这些标注选择 checked 路径。

## 允许的级别

| 级别 | 覆盖                              | v0.3.x 允许的声明                   |
| ---- | --------------------------------- | ----------------------------------- |
| S1   | aliasing / exclusivity            | 最多 `checked`，不得声明 `enforced` |
| S2   | `Send` / `Sync`                   | 最多 `checked`，不得声明 `enforced` |
| S3   | region / `Pin` / interface safety | 推迟到 v0.4.0                       |

`checked` 表示前端或 fixture 对某个具体交互验证过该属性。`enforced` 表示 ABI 或运行时能够机械地阻止违规。

v0.3.x 的强约束是 release 协议：

- release 恰好执行一次；
- release 回到创建模块及其 allocator；
- release 入口不得 unwind。

这些是契约要求。fixture 可以检查它们，但核心 ABI 不会把它们扩展成通用运行时安全系统。

## 缺少元数据

- 缺少标注表示 `unspecified`。
- 消费者不得从“没有标注”推断 `borrowed`、`owned`、`Send`、`Sync`、no-alias 或线程安全。
- 新增标注是 additive 的：不改变现有记录，也不改变兼容性结果。

## 兼容性

Safety metadata 不属于 ABI identity。`amc verify` 可以单独报告 `safety_changed` drift，但不会把它折叠进单一的“ABI compatible”结论。
