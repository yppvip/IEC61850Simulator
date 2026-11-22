# Task9 · T9.1 引用依据投影

2026-11-22。后端门槛通过。

`SclReferenceValues` 保留原复制视图，并可另行交付按原始声明区分的引用来源。`SclReferenceTrace` 按根或单次分支请求生成节点。实例地址证据来自 MMS 投影，FCDA 与 ExtRef 目标路径来自既有解析过程。投影从组成项重组字符串核对根值，证据不足时只给出已知内容及限制。

清洁 PATH 下 `SclReferenceResolverTest` 和 `SclMmsQueryTest` 通过（2/2）。新增用例核对五类根值、显式与推导 LD 名、前缀和 AP 别名、模板声明与多个候选、FC 冲突、DO 级路径、数组原始与有效下标、同值不同声明、多行标签起始行、取消及节点上限。样本由现有的自建 XML 夹具和测试内变体提供。
