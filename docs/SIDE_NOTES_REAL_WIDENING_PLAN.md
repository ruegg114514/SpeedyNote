# 笔记栏重构方案：从「平行结构」改为「页面的一部分」

> 分支：`work-palm`
> 状态：阶段 0+1 已落地，阶段 2 待做
> 一句话：**把笔记栏从"页面旁边的另一个东西"改成"页面本身变宽多出来的那一块"。**

---

## 1. 结论

你的判断是对的，而且比想的更彻底：当前实现**视觉上已经加宽了，但数据上是两套东西**。

页面 `Page::size` 保持 PDF 原始尺寸不动，笔记栏是画在页面矩形**右侧外面**的一层 overlay，
配一套平行的宽度表、笔画表、像素缓存、溢出绘制、分隔线拖动、lasso 索引、undo 分流和持久化。

这不是"没优化好"，而是**架构选型带来的固有成本**：笔记栏每多接一个功能，
就必须在每一条通用路径上再写一遍特判。继续做优化只是把特判打磨得更顺滑，
不会减少特判的数量。

正确做法是让加宽是**真实的**：改 `page->size`，笔记笔画存进页面正常的 `VectorLayer`。

---

## 2. 现状：虚拟加宽 = 两套数据

### 2.1 数据清单

| 位置 | 内容 | 说明 |
| --- | --- | --- |
| `DocumentViewport.h:3954` | `QMap<int,qreal> m_sideNotesWidths` | 逐页笔记栏宽度，页面尺寸不含它 |
| `DocumentViewport.h:3963` | `QMap<int,QVector<VectorStroke>> m_sideNotesStrokes` | 笔记笔画，独立于 `Page::vectorLayers` |
| `DocumentViewport.h:3964-3966` | `m_sideNotesCurrentStroke` 等 | 在笔记栏里另有一套落笔状态机 |
| `DocumentViewport.h:3979` | `QHash<int,NotesColumnCacheEntry> m_notesColumnCache` | 笔记栏自己一套像素缓存 |
| `DocumentViewport.h:3340` | `m_lassoNotesPage` / `m_lassoNotesIndices` | lasso 单独维护的平行索引 |
| `DocumentViewport.h:148` | `UndoAction::StrokeSegment::fromNotes` | undo 靠这个 flag 分流回哪张表 |
| `DocumentViewport.cpp:22259` | `saveSideNotes()/loadSideNotes()` | 存到 `side_notes.json`，不走页面 JSON |

### 2.2 特判点（按调用位置）

- **绘制**：`DocumentViewport.cpp:3324`、`3475` 调 `drawNotesColumn`
- **溢出绘制**：`DocumentViewport.cpp:21958 drawNotesColumnOverflow`（笔画被扫到页面本体/越过远边时要补画）
- **对象盖在笔记栏上**：`DocumentViewport.h:5059-5062` 要把与笔记栏重叠的对象再画一遍
- **落笔路由**：`DocumentViewport.cpp:6676` 命中笔记栏 → `startNotesStroke`；`6805` 续笔；`7017` 收笔
- **lasso 路由**：`DocumentViewport.cpp:8079-8084` 命中笔记栏时切到平行索引路径
- **undo/redo 分流**：`DocumentViewport.cpp:19482`、`19566`、`19977`、`20061` 四处按 `fromNotes` 分支
- **lasso 删除/搬移**：`13648`、`14950-15093`、`15500` 三处专门处理笔记表
- **布局**：`ensurePageLayoutCache` 在 `6104`、`6136-6137`、`6148` 把 `sideNotesWidthFor(i)` 加到内容宽度里
- **命中/夹取**：`2341`（`clampObjectPositionToPage` 特意放宽右边界到 `pageSize.width()+notesW`）、`9270`
- **分隔线拖动**：`21531 notesDividerPageAtViewport`、`3944`、`5556`（含触摸版）
- **导出**：`MuPdfExporter.cpp:2072 renderNotesColumnPage`、`750/761/827` 的 notes-only 分支
- **入口**：`MainWindow.cpp:9036-9053` 的 toggle

全仓库 `sideNotes|fromNotes` 命中 **128 处**，其中 `DocumentViewport.cpp` 内 128 个引用点。

### 2.3 根因（唯一的拦路虎）

`DocumentViewport.cpp:20582 renderPage()`：

```cpp
QSizeF pageSize = page->size;
QRectF pageRect(0, 0, pageSize.width(), pageSize.height());
...
case Page::BackgroundType::PDF:
    ...
    painter.drawPixmap(pageRect.toRect(), pdfPixmap);   // ← :20628 PDF 拉伸铺满整页
```

PDF 位图被**拉伸**到整个 `pageRect`。所以裸改 `page->size` 会把 PDF 拉变形 ——
这正是当初不得不另起一套 overlay 的原始原因。**这一处修掉，其余特判全部可以删。**

---

## 3. 目标设计

### 3.1 新增唯一字段

```cpp
// Page.h
qreal bodyWidth = 0.0;   ///< 正文（背景）区宽度。0 = 无笔记栏，正文占满整页。

/// 笔记栏宽度（0 表示没有笔记栏）
qreal notesWidth() const {
    return (bodyWidth > 0.0 && bodyWidth < size.width())
           ? size.width() - bodyWidth : 0.0;
}
QRectF bodyRect() const { return QRectF(0, 0, bodyWidth > 0.0 ? bodyWidth : size.width(), size.height()); }
```

**不变量**

1. `Page::size` = 全部可视区域 = 正文 + 笔记栏
2. `bodyWidth` 一旦写入就不再依赖 PDF 是否存在（PDF 缺失时页面仍能正确渲染）
3. 有笔记栏 ⟺ `bodyWidth > 0 && bodyWidth < size.width()`
4. 笔记栏高度 = 页面高度（不独立滚动）
5. 所有笔画（正文 + 笔记）都在**页面局部坐标**里，存在 `Page::vectorLayers` 中

### 3.2 渲染规则

| 图元 | 绘制范围 |
| --- | --- |
| 页底色（白边） | `pageRect`（整页）→ 这就是"多出来的白边" |
| PDF / Custom 背景 | `bodyRect` |
| Grid / Lines 图案 | `bodyRect`（笔记区想要点阵/横线，另画一层浅色） |
| 笔画、对象 | 整页（原有路径，无需改动） |
| 页边框 | `pageRect` |
| 分隔线 | `x = bodyWidth`，可拖动 |
| 命中测试 / 缩略图 / 滚动 / 两栏布局 | `pageRect`（原有路径） |

---

## 4. 改动清单

### 4.1 `Page.h` / `Page.cpp`

- 加 `bodyWidth` 字段 + `notesWidth()` / `bodyRect()` helper
- `toJson` 写入 `"bodyWidth"`；`fromJson` 读取，缺省 0（老文档自动解释为"无笔记栏"）
- PDF 页创建时把 `bodyWidth` 一次性写成当时 `size.width()`（见 4.3）

### 4.2 `Document.h` / `Document.cpp`

- `setPageSize` 保持现状即可（已同步 `m_pageMetadata`）
- 新增 `setPageBodyWidth(int index, qreal bodyWidth)`：写 `Page::bodyWidth` + 置 dirty + 同步 `pageSizeAt`
- `Document.cpp:1864-1870`（`createForPdf`）与 `1529-1537`（懒加载回填尺寸）两处：
  设完 `page->size = pdfSize * 96.0/72.0` 之后，紧跟 `page->bodyWidth = page->size.width();`

### 4.3 `DocumentViewport::renderPage()` —— 唯一真正的新代码

- 背景 fill / PDF / Custom / Grid / Lines 的绘制矩形从 `pageRect` 换成 `page->bodyRect()`
- 页边框仍画 `pageRect`
- 新增 `drawNotesDivider(painter, page)`：在 `x = bodyWidth` 画 1px 分隔线 + 拖动握把
- **不新增其它任何东西**

> 提示：`painter.drawPixmap(bodyRect, pdfPixmap)` 之后缩放比与现在完全一致
> （bodyRect 高度 = 页高，PDF 宽高比自洽），所以 PDF 分辨率不受影响，
> `effectivePdfDpi()`（`DocumentViewport.cpp:21265`）无需改动。

### 4.4 删除清单

| 删除对象 | 位置 |
| --- | --- |
| `m_sideNotesWidths` 及全部读写 | `DocumentViewport.h:3954` 等 |
| `m_sideNotesStrokes` / `CurrentStroke` / `IsDrawing` / `ActivePage` | `:3963-3966` |
| `NotesColumnCacheEntry` / `m_notesColumnCache` / `m_notesCacheZoom/Dpr` | `:3975-3981` |
| `m_lassoNotesPage` / `m_lassoNotesIndices` | `:3340` |
| `UndoAction::StrokeSegment::fromNotes` 及四处分流 | `:148`、`.cpp:19482/19566/19977/20061` |
| `startNotesStroke` / `continueNotesStroke` / `endNotesStroke` / `drawNotesStroke` | `.cpp:21583-21745` |
| `drawNotesColumn` / `drawNotesColumnOverflow` | `.cpp:21747` / `21958` |
| `notesPageAtViewport` / `notesDividerPageAtViewport` | `.cpp:21567` / `21531` |
| `saveSideNotes` / `loadSideNotes` | `.cpp:22245` / `22306` |
| `MuPdfExporter::renderNotesColumnPage` + notes-only 分支 | `MuPdfExporter.cpp:2072`、`750/761/827` |
| lasso 三处笔记特判 | `.cpp:13648`、`14950-15093`、`15500` |
| `clampObjectPositionToPage` 的 notesW 分支 | `.cpp:2339-2359` |
| `ensurePageLayoutCache` 里的 `sideNotesWidthFor` 加法 | `.cpp:6104`、`6136-6137`、`6148` |

### 4.5 输入路由简化

`DocumentViewport.cpp:6676 / 6805 / 7017 / 8079-8084`：命中笔记栏时不再另走分支。
笔记区在 `pageRect` 内，`pageAtPoint` 自然命中该页，普通笔画管线直接可用。

唯一需要决定的新规则：**是否允许在正文区写字**（现状是笔记栏和正文互不干扰）。
若要保持，在落笔时判断 `pageLocal.x() < bodyWidth` 是否拒绝即可 —— 一个可选约束，
默认建议**允许**（反正正文也是同一张纸，用户可能想直接批注）。

### 4.6 分隔线拖动

拖动 → `Document::setPageBodyWidth(pageIndex, newX)`：

- 同时调 `setSideNotesWidth` 等价的旧入口（改为改页面尺寸）
- 置 `m_pageLayoutDirty = true` 并 `ensurePageLayoutCache()`
- `markPageDirty(pageIndex)` + `emit documentModified()`（现在它存 `side_notes.json`，改完随页面存）
- 建议加 undo（新类型 `PageBodyWidth`），因为现在它是个可撤销的页面几何变更
- **最小宽度规则**：`bodyWidth` 不得小于"该页最右侧笔画 / 对象的右边缘"，
  这样缩窄笔记栏不会把已有墨迹裁掉（替代原来的 `drawNotesColumnOverflow` 兜底）

### 4.7 需要拍板的收尾项

1. **`zoomToFit` / 适应宽度**（`.cpp:1824`）：现在会把整张加宽页缩进视口，PDF 正文会变小。
   建议"适应宽度"按 `bodyRect` 算，这样开笔记栏不会让正文突然缩小。
2. **缩略图**（`ThumbnailRenderer` / `PagePanel`）：加宽页宽高比变了。
   建议缩略图按 `pageRect` 画以保持"一整页"的观感，但内容按比例缩小 —— 需要确认缓存 key 是否含尺寸。
3. **两栏自动布局阈值**：`setAutoLayoutEnabled` 的判断用页面宽度，加宽后阈值随之变化。
   `pagePosition` 的 TwoColumn 分支（`.cpp:2045-2061`）已经是逐行取左侧页宽，逻辑本身正确。
4. **PDF 导出**：页面变宽，导出的 PDF 页面也变宽。
   建议给导出加一个选项：`裁切到正文` / `保留笔记栏`（等价于现在的 notes-only，但由通用渲染裁剪实现）。
5. **新建页面继承**：上一页有笔记栏时，新页是否继承同样的 `bodyWidth`。建议继承。

---

## 5. 分阶段实施（每阶段可编译、可验证）

### 阶段 0：准备（无行为变化）
- `Page` 加 `bodyWidth` + helper + 序列化
- `createForPdf` / 懒加载回填处写入 `bodyWidth = size.width()`
- 此时所有页 `bodyWidth == size.width()`，`notesWidth() == 0`，行为与今天完全一致

### 阶段 1：渲染切换到 bodyRect
- `renderPage` 的背景绘制改用 `bodyRect()`
- 此时仍无笔记栏，但渲染路径已经"知道"正文区在哪 —— 用截图对比验证像素级一致
- **这是风险最高的一步，单独提交，单独回归**

### 阶段 2：真实加宽 + 双轨并存
- 新增 `setPageBodyWidth`；toggle 改为"加宽页面 + 设 bodyWidth"
- 旧的 overlay 暂时保留但不再使用（便于回退对比）
- 笔记笔画暂时**仍然**写 `m_sideNotesStrokes`，但坐标改成页面局部
  （这样可以先只验证几何，不触碰 undo/lasso）

### 阶段 3：笔画归位
- 笔记笔画改写入 `page->activeLayer()`
- 删 `fromNotes`、undo 四处分流、lasso 三处特判、`drawNotesColumn*`、独立缓存
- 这一步删掉的代码最多，收益也最大

### 阶段 4：清理与迁移
- 删 `saveSideNotes` / `loadSideNotes`、`renderNotesColumnPage`、布局里的宽度加法
- 写迁移：打开文档时若存在 `side_notes.json`，把有宽度的页 `setPageSize(width+notesW, height)`、
  设 `bodyWidth`、把笔记笔画整体 X 平移 `+bodyWidth` 后并入页面图层
- 迁移完成后该文件可忽略（建议保留一份 `.bak`，不主动删）

### 阶段 5：收尾项
- `zoomToFit` 按 `bodyRect`
- 缩略图、两栏阈值、导出选项、新建页继承
- 分隔线拖动 + undo + 最小宽度规则

---

## 6. 风险与取舍

| 项目 | 评估 |
| --- | --- |
| 页码/滚动 | 加宽只影响水平方向，Y 布局不变，页码稳定 |
| 笔画缓存内存 | 页面缓存尺寸变大，但同时删掉了笔记栏独立缓存 —— **净持平**。`chooseRenderTier` 的 Capped/Focus 分级会自动兜底 |
| 老文档兼容 | `bodyWidth` 缺省 0 → 无笔记栏，行为不变；有 `side_notes.json` 的走阶段 4 迁移 |
| PDF 缺失 | `bodyWidth` 显式存盘，不依赖 PDF 在线，比"从 PDF 反推"稳 |
| 能力损失 | 笔记栏不再能高于页面 / 独立滚动 / 独立缩放。这本来就是"和正文是一个东西"的代价，符合你的目标 |
| 导出语义 | 页面变宽是显式行为，需一个裁切选项兜住"只要正文"的需求 |
| 回归重点 | 阶段 1（渲染）和阶段 3（undo/lasso）必须分两次单独回归；lasso 跨正文-笔记栏选择会成为**新能力**，需专门测 |

---

## 7. 验收清单

- [ ] 无笔记栏的文档，改造前后**像素级一致**（阶段 1 退出条件）
- [ ] 开笔记栏后 PDF 不变形，正文尺寸不缩小
- [ ] 笔记栏里写字：undo / redo / 橡皮 / lasso 选中 / 复制粘贴 全部与正文行为一致
- [ ] lasso 可同时框选正文与笔记栏笔画
- [ ] 关闭再打开：宽度与笔画都在（`side_notes.json` 不再产生）
- [ ] 导出 PDF：页面宽度正确，裁切选项生效
- [ ] 缩略图 / 大纲 / 链接跳转页定位正确
- [ ] 拖分隔线缩窄时不裁掉已有墨迹
- [ ] 老文档（含 `side_notes.json`）打开后笔记栏位置与内容不变

---

## 8. 实现进度

### 已完成：阶段 0 + 阶段 1

**行为零变化**，只是把"接缝"放好。`bodyWidth` 对现存每一页都是 0，
所以 `bodyRect() == 整页矩形`，渲染路径逐像素不变。

| 文件 | 改动 |
| --- | --- |
| `source/core/Page.h` | 新增 `bodyWidth` 字段 + `notesWidth()` / `bodyRect()`；补 `#include <QRectF>` |
| `source/core/Page.cpp` | `toJson` 仅在存在笔记栏时写 `bodyWidth`（保证老笔记本字节不变）；`fromJson` 读取；`renderBackground` 的背景绘制与图案矩形改用 `bodyRectZoomed`，并补一次整页底色填充 |
| `source/core/DocumentViewport.cpp` | `renderPage` 新增 `bodyRect` / `bodySize`；PDF、自定义背景、Grid/Lines 图案缓存全部改画到 body 区域 |

`renderBackgroundPattern`（None 分支）与 PDF/Custom 之外的路径：整页底色由新增的
`painter.fillRect(pageRect, backgroundColor)` 负责，图案只覆盖 body，笔记条保持素纸色。

### 阶段 2 必须一并处理的两个延迟点

阶段 0+1 里**故意没碰**这两个位置 —— 它们只在 `bodyWidth > 0` 时才会出错，
而现阶段 `bodyWidth` 恒为 0，所以留着是安全的。阶段 2 打开真实加宽时必须一起改：

1. **`source/ui/ThumbnailRenderer.cpp:381`** —— PDF 背景按 `pageRect` 铺满缩略图。
   `ThumbnailSnapshot` 里没有 `bodyWidth`，需要给快照结构体加字段并在抓取处填值，
   然后渲染时改用 body 矩形。
2. **`source/pdf/MuPdfExporter.cpp`** —— 导出路径同样需要把源 PDF 放进 body 区域，
   而不是页面的整张矩形。同时决定 `裁切到正文 / 保留笔记栏` 选项（见 4.7 第 4 条）。

### 阶段 2 的入口

`Document::setPageBodyWidth(int index, qreal bodyWidth)` 尚未添加（阶段 0 不加未使用的 API）。
阶段 2 第一步就是加它：写 `Page::bodyWidth`、`markPageDirty`、同步 `pageSizeAt`、
置 `m_pageLayoutDirty`。之后把 toggle 从"建 overlay"改成"加宽页面 + 设 bodyWidth"即可。

### 协作备注：本地副本与远程一致性

改动前后都核对过：本地工作副本与远程 `work-palm` HEAD 的
`Page.h` / `Page.cpp` / `DocumentViewport.cpp` **逐字节一致**。
如果以后再出现"锚点全不命中"，先核对这一点 —— 最容易的坑是把
Git blob API 返回的 JSON wrapper 当成文件内容（它是 base64，不是明文）。
