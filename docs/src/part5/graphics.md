# 图形与多媒体库

这一部分介绍 YXG 的 Window / Canvas / Components / Music / Video 库，以及相关的常量约定。

## 核心约定（务必先读）

### 1. GC 只扫描全局变量

YXG 的垃圾回收器**只把全局变量当作根**。任何需要在多帧之间存活的对象（窗口、画布、控件、音频、视频、socket）**必须声明为全局变量**：

```yxg
import yxg.Window;

// ❌ 错误：局部变量会在一帧后被 GC 回收
pub void main() {
    Window w = new Window();
    w.create("test", 200, 100);
    print w.get_width(), "\n";      // 可能读到垃圾值
}

// ✅ 正确：声明为全局
Window w;

pub void main() {
    w = new Window();
    w.create("test", 200, 100);
    print w.get_width(), "\n";
}
```

实测对比：局部 `Window` 的 `get_width()` 返回了 `184`（应为 200），全局声明则正常。

### 2. 两段式构造

控件和画布都是「`new` 构造 → `create(...)` 初始化」两步：

```yxg
Canvas c = new Canvas();     // 第一步：构造（内部句柄为 0）
c.create(64, 64);            // 第二步：分配真实资源
```

例外：`Canvas.load()` 和 `Music.load()` 内部会自动初始化，**不要先 create**。

### 3. 颜色格式

颜色是 `int`，格式 `0xRRGGBB`：

| 名称 | 值 | 名称 | 值 |
|---|---|---|---|
| `WindowColor.COLOR_BLACK` | `0x000000` | `WindowColor.COLOR_WHITE` | `0xFFFFFF` |
| `WindowColor.COLOR_RED` | `0xFF0000` | `WindowColor.COLOR_GREEN` | `0x00FF00` |
| `WindowColor.COLOR_BLUE` | `0x0000FF` | `WindowColor.COLOR_YELLOW` | `0xFFFF00` |
| `WindowColor.COLOR_CYAN` | `0x00FFFF` | `WindowColor.COLOR_MAGENTA` | `0xFF00FF` |
| `WindowColor.COLOR_GRAY` | `0x808080` | `WindowColor.COLOR_DARK_GRAY` | `0x404040` |
| `WindowColor.COLOR_LIGHT_GRAY` | `0xC0C0C0` | `WindowColor.COLOR_ORANGE` | `0xFF8000` |
| `WindowColor.COLOR_PURPLE` | `0x800080` | `WindowColor.COLOR_TRANSPARENT` | `0xFF00FF` |

透明贴图的魔法色是**品红 `0xFF00FF`**——像素等于它时，透明 blit 会跳过。

### 4. 输入是轮询式

不是事件回调，每帧主动查询：

```yxg
win.is_key_down(WindowKey.KEY_LEFT)      // 按住期间持续为真
win.is_key_pressed(WindowKey.KEY_SPACE)  // 只有刚按下那一帧为真
```

只有**获得焦点的窗口**能收到输入。

### 5. 帧循环模板

```yxg
while win.is_valid() != 0 {
    // 1. 读输入
    // 2. 更新逻辑
    win.clear(WindowColor.COLOR_BLACK);
    // 3. 绘制
    win.flush();       // 后台缓冲刷到屏幕
    win.update();      // 处理窗口消息
    win.sleep(16);     // 限帧约 60fps
}
```

## 常量速查

这些常量都是**枚举成员**，用的时候要带枚举前缀，不能裸写：

| 枚举 | 前缀 | 例 |
|---|---|---|
| `WindowColor` | `WindowColor.` | `WindowColor.COLOR_BLACK` |
| `WindowKey` | `WindowKey.` | `WindowKey.KEY_SPACE` |
| `WindowMouse` | `WindowMouse.` | `WindowMouse.MOUSE_LEFT` |
| `WindowCursor` | `WindowCursor.` | `WindowCursor.CURSOR_HAND` |
| `PopupResult` | `PopupResult.` | `PopupResult.POPUP_OK` |
| `DrawState` | `DrawState.` | `DrawState.STATE_PRESSED` |

```yxg
win.clear(WindowColor.COLOR_BLACK);
win.draw_text(10, 10, "得分", WindowColor.COLOR_WHITE);
if win.is_key_down(WindowKey.KEY_LEFT) { /* ... */ }
```

### 按键

| 常量 | 值 | 常量 | 值 |
|---|---|---|---|
| `WindowKey.KEY_BACKSPACE` | 8 | `WindowKey.KEY_TAB` | 9 |
| `WindowKey.KEY_ENTER` | 13 | `WindowKey.KEY_ESC` | 27 |
| `WindowKey.KEY_SPACE` | 32 | `WindowKey.KEY_LEFT`/`UP`/`RIGHT`/`DOWN` | 37/38/39/40 |
| `WindowKey.KEY_0`~`WindowKey.KEY_9` | 48~57 | `WindowKey.KEY_A`~`WindowKey.KEY_Z` | 65~90 |
| `WindowKey.KEY_F1`~`WindowKey.KEY_F12` | 112~123 | | |

### 鼠标

| 常量 | 值 |
|---|---|
| `WindowMouse.MOUSE_LEFT` | 0 |
| `WindowMouse.MOUSE_RIGHT` | 1 |
| `WindowMouse.MOUSE_MIDDLE` | 2 |

### 光标样式

| 常量 | 值 |
|---|---|
| `WindowCursor.CURSOR_ARROW` | 0 |
| `WindowCursor.CURSOR_HAND` | 1 |
| `WindowCursor.CURSOR_CROSS` | 2 |
| `WindowCursor.CURSOR_IBEAM` | 3 |
| `WindowCursor.CURSOR_WAIT` | 4 |

### 自绘状态位

| 常量 | 值 |
|---|---|
| `DrawState.STATE_PRESSED` | 1 |
| `DrawState.STATE_DISABLED` | 4 |
| `DrawState.STATE_FOCUS` | 16 |
| `DrawState.STATE_HOT` | 64 |

### 弹窗返回值

| 常量 | 值 |
|---|---|
| `PopupResult.POPUP_OK` | 1 |
| `PopupResult.POPUP_CANCEL` | 2 |
| `PopupResult.POPUP_YES` | 6 |
| `PopupResult.POPUP_NO` | 7 |

## Window 窗口库

导入：`import yxg.Window;`

### 创建与生命周期

| 方法 | 说明 |
|---|---|
| `create(string title, int w, int h)` | 创建窗口，尺寸是客户区 |
| `create(int w, int h)` | 无标题版本 |
| `show()` / `hide()` | 显示 / 隐藏 |
| `close()` | 销毁 |
| `is_valid()` | 是否存活，主循环条件 |

```yxg
Window win;

pub void main() {
    win = new Window();
    win.create("标题", 800, 600);
    win.show();
    win.center();
    win.close();
}
```

### 窗口属性

| 方法 | 说明 |
|---|---|
| `set_title(string)` | 改标题 |
| `move(int x, int y)` | 移动 |
| `resize(int w, int h)` | 改尺寸 |
| `get_width()` / `get_height()` | 客户区尺寸 |
| `center()` | 居中 |

### 消息循环

| 方法 | 说明 |
|---|---|
| `update()` | 非阻塞处理消息，返回 1 表示收到退出 |
| `wait()` | 阻塞直到退出 |
| `flush()` | 刷新缓冲到屏幕 |

### 基础绘制

坐标原点在客户区左上角，y 轴向下。

| 方法 | 说明 |
|---|---|
| `clear(int color)` | 清屏 |
| `set_pixel(x, y, c)` | 画点 |
| `draw_line(x1,y1,x2,y2,c)` | 直线 |
| `draw_line_thick(..., c, w)` | 指定粗细 |
| `draw_rect` / `fill_rect` | 空心 / 实心矩形 |
| `draw_round_rect` / `fill_round_rect` | 圆角矩形 |
| `draw_circle` / `fill_circle` | 圆 |
| `draw_triangle` / `fill_triangle` | 三角形 |

```yxg
win.draw_circle(400, 300, 50, WindowColor.COLOR_RED);
win.fill_circle(400, 300, 48, WindowColor.COLOR_YELLOW);
```

### 文字

| 方法 | 说明 |
|---|---|
| `draw_text(int x, int y, string, int c)` | 左上对齐 |
| `draw_text_center(int y, string, int c)` | 水平居中 |
| `draw_char(int x, int y, int ch, int c)` | 单个 ASCII 字符 |
| `set_font_size(int size)` | 字号（静态/实例调用都可以） |

```yxg
Window.set_font_size(24);
win.draw_text(10, 10, "得分", WindowColor.COLOR_WHITE);
win.draw_text_center(50, "标题", WindowColor.COLOR_YELLOW);
```

### 贴图

| 方法 | 说明 |
|---|---|
| `draw_canvas(Canvas, x, y)` | 不透明整图 |
| `draw_canvas_part(Canvas, sx,sy,sw,sh, dx,dy)` | 局部（精灵图集） |
| `draw_canvas_scaled(Canvas, x,y,w,h)` | 缩放 |
| `draw_canvas_transparent(Canvas, x, y)` | 透明（品红跳过） |
| `draw_canvas_part_transparent(...)` | 局部透明 |
| `draw_canvas_alpha(Canvas, x, y)` | 带 alpha 通道 |
| `draw_canvas_part_alpha(...)` | 局部带 alpha |

### 键盘与鼠标

| 方法 | 说明 |
|---|---|
| `is_key_down(int key)` | 按住 |
| `is_key_pressed(int key)` | 刚按下 |
| `is_mouse_down(int btn)` | 鼠标按住 |
| `is_mouse_clicked(int btn)` | 刚点击 |
| `get_mouse_x()` / `get_mouse_y()` | 鼠标位置 |
| `get_mouse_wheel()` | 滚轮增量（读后清零） |

### 文字输入与光标

| 方法 | 说明 |
|---|---|
| `get_char()` | 取一个输入字符（ASCII），无输入返回 0 |
| `set_cursor(int style)` | 设置光标样式 |

### 定时器

| 方法 | 说明 |
|---|---|
| `start_timer(int ms)` | 启动周期定时器 |
| `stop_timer()` | 停止 |
| `get_timer_hits()` | 命中次数（读后清零） |

```yxg
win.start_timer(1000);
while win.is_valid() != 0 {
    if win.get_timer_hits() > 0 {
        // 每秒执行一次
    }
}
```

### 时间

| 方法 | 说明 |
|---|---|
| `get_ticks()` | 毫秒计时器 |
| `sleep(int ms)` | 休眠 |

```yxg
int t0 = Window.get_ticks();
// ...
int dt = Window.get_ticks() - t0;
```

### 窗口状态

| 方法 | 说明 |
|---|---|
| `is_valid()` | 句柄是否有效（`0` 表示窗口已销毁） |
| `is_minimized()` / `is_maximized()` / `is_visible()` | 状态查询，返回 `1`/`0` |
| `set_focus()` / `has_focus()` | 置焦点 / 查焦点 |
| `set_topmost(int)` | 置顶，`1` 开 `0` 关 |
| `set_resizable(int)` | 是否允许拖边框改变大小 |
| `set_maximizable(int)` | 是否允许最大化 |
| `minimize()` / `maximize()` / `restore()` | 最小化 / 最大化 / 还原 |
| `set_fullscreen(int)` | 无边框全屏，`1` 进入 `0` 退出（见下） |

**布尔参数一律传 `int`**：写 `set_maximizable(1)`，不是 `true`。字面量 `true`/`false` 走的是整型路径，`,` 后面接的必须是整数表达式。

#### 固定窗口尺寸

禁止用户改大小、也禁止最大化，两个都要设：

```yxg
import yxg.Window;

Window win;

pub void main() {
    win = new Window();
    win.create("固定尺寸窗口", 640, 480);

    win.set_resizable(0);      // 拖边框不能改大小
    win.set_maximizable(0);    // 标题栏最大化按钮变灰

    while win.update() != 0 {
        win.clear(0xFFFFFF);
        win.flush();
    }
}
```

`set_maximizable(0)` 和 `set_resizable(0)` 是两件事：前者只管最大化，后者只管拖拽缩放，**只设一个仍会留下另一个入口**。


#### 全屏 `set_fullscreen`

`set_fullscreen(1)` 进入全屏、`set_fullscreen(0)` 退出。它做的是**无边框全屏**：
把窗口样式换成 `WS_POPUP`，再用 `SetWindowPos` 铺满整个屏幕，不是独占式/排他全屏。

```yxg
import yxg.Window;

Window win;

pub void main() {
    win = new Window();
    win.create("全屏测试", 640, 480);
    win.show();

    win.set_fullscreen(1);          // 进入：去边框 + 铺满屏幕

    while win.update() != 0 {
        win.clear(WindowColor.COLOR_BLACK);

        if win.is_key_down(WindowKey.KEY_ESC) != 0 {
            win.set_fullscreen(0);  // 退出：样式和窗口矩形都还原
        }

        win.flush();
    }
}
```

**可复原**：进入前会把原来的窗口样式和窗口矩形保存下来，退出时原样写回。
实测（1920×1080 屏、`create(..., 640, 480)`，客户区 624×441）：

```
before        : 624x441
fullscreen    : 2560x1440
again (idem)  : 2560x1440
restored      : 624x441
restore again : 624x441
is_visible=1 is_valid=1
```

注意第三行 —— **重复调用同一方向是幂等的**：已经在全屏时再传 `1` 不会二次保存样式，
退出时仍能还原回 `624x441`；还没进全屏时传 `0` 也是空操作。

几点要注意：

- 参数是 `int`，写 `set_fullscreen(1)`，不要写 `true`。
- 全屏后窗口样式变了，`get_width()` / `get_height()` 返回的是**屏幕尺寸**（上例 2560×1440，
  含 DPI 缩放），不是 `create` 时传的客户区大小。按屏幕尺寸重新布局即可。
- 想还原到全屏前的尺寸，只能靠 `set_fullscreen(0)`；手动 `resize()` 不会恢复原样式。
- 进去之后标题栏、边框、最大化按钮全没了，**务必留一个退出热键**（上例是 ESC），
  否则全屏窗口没法用鼠标关。

#### 主动最大化 / 还原

```yxg
win.maximize();
print "是否最大化: ", win.is_maximized(), "\n";   // 1

win.restore();
print "是否最大化: ", win.is_maximized(), "\n";   // 0
```

#### 置顶窗口

```yxg
win.set_topmost(1);    // 始终显示在最前面
win.set_topmost(0);    // 取消
```

> ⚠️ 这些方法都依赖内部窗口句柄，**必须在 `create()` 之后调用**。
> 在 `new Window()` 之前或 `create()` 之前调用会拿到无效句柄，程序直接崩（`0xC0000005`）。
### 关闭拦截

```yxg
win.set_close_blocked(1);
while win.is_valid() != 0 {
    if win.is_close_requested() != 0 {
        // 存档等清理
        win.set_close_blocked(0);
        win.clear_close_request();
    }
    // ...
}
```

### 文件对话框

```yxg
string path = win.open_file("选择文件");
string save = win.save_file("保存文件");
// 取消返回空串 ""
```

### 弹窗

| 方法 | 返回 |
|---|---|
| `Window.popup_info(text, title)` | 信息框 |
| `Window.popup_warning(...)` | 警告框 |
| `Window.popup_error(...)` | 错误框 |
| `Window.popup_question(...)` | 询问框 |
| `Window.popup_yes_no(...)` | 是/否 |
| `Window.popup_yes_no_cancel(...)` | 是/否/取消 |
| `Window.popup_ok_cancel(...)` | 确定/取消 |
| `Window.popup_retry_cancel(...)` | 重试/取消 |
| `Window.popup_input(prompt, title)` | 输入框，返回 string |

### 自动布局

```yxg
Window.layout_begin(10, 10, 8, 400);   // x, y, 间距, 最大宽度
btn1.create(win, "按钮1", -1, -1, 100, 30);   // x=-1,y=-1 自动排列
btn2.create(win, "按钮2", -1, -1, 100, 30);
Window.layout_space(10);                // 额外留空
Window.layout_end();
```

### 自绘控件（DC 绘图）

仅在 `onPaint` / 自绘回调内使用：

```yxg
Window.dc_fill_rect(dc, x, y, w, h, color);
Window.dc_draw_rect(dc, x, y, w, h, color);
Window.dc_set_pixel(dc, x, y, color);
Window.dc_draw_line(dc, x1, y1, x2, y2, color);
Window.dc_fill_circle(dc, cx, cy, r, color);
Window.dc_draw_circle(dc, cx, cy, r, color);
Window.dc_draw_text(dc, x, y, text, color);
int w = Window.dc_get_text_width(dc, text);
Window.dc_fill_gradient(dc, x, y, w, h, topColor, bottomColor);
Window.dc_fill_gradient_round(dc, x, y, w, h, radius, topColor, bottomColor);
```

### 多窗口管理

```yxg
int count = Window.get_count();
int hwnd = Window.get_at(i);
```

### 其它

| 方法 | 说明 |
|---|---|
| `message_box(string text, string title)` | 简单消息框 |
| `onPaint(int dc)` | 虚方法，可重写做窗口自绘 |

## Canvas 画布库

Canvas 定义在 `yxg.Window` 模块里（`Window.yxg` 中包含 `Canvas` 类）。

Canvas 是离屏像素缓冲，可当图片、精灵、纹理、视频帧：

```yxg
Canvas c;

pub void main() {
    c = new Canvas();
    c.create(64, 64);
    c.clear(WindowColor.COLOR_MAGENTA);
    c.fill_circle(32, 32, 20, WindowColor.COLOR_GREEN);
}
```

| 方法 | 说明 |
|---|---|
| `create(int w, int h)` | 创建，返回 1 成功 |
| `destroy()` | 释放 |
| `get_width()` / `get_height()` | 尺寸 |
| `clear(int color)` | 清屏 |
| `set_pixel(x, y, c)` | 画点 |
| `fill_rect` / `draw_rect` | 矩形 |
| `draw_line(x1,y1,x2,y2,c)` | 直线 |
| `fill_circle` / `draw_circle` | 圆 |
| `draw_text(x, y, string, c)` | 文字 |
| `get_pixel(x, y)` | 读像素 |
| `load(string path)` | 从文件加载图片（BMP/PNG/JPEG），自动初始化 |

### 生成精灵

```yxg
Canvas sprite;

pub void main() {
    sprite = new Canvas();
    sprite.create(32, 32);
    sprite.clear(WindowColor.COLOR_TRANSPARENT);       // 品红底
    sprite.fill_circle(16, 16, 12, WindowColor.COLOR_GREEN);

    // 贴到窗口时品红自动透明
    // win.draw_canvas_transparent(sprite, 100, 100);
}
```

### 加载图片

```yxg
Canvas img;

pub void main() {
    img = new Canvas();
    int ok = img.load("sprite.png");
    if ok == 1 {
        print "尺寸: ", img.get_width(), "x", img.get_height(), "\n";
    }
}
```

`load` 会自动分配画布，**不要先 create**。透明 blit 只认品红。

## 组件库 Components

导入：`import yxg.Components;`

所有控件都是「`new` → `create(parent, ...)`」，`parent` 必须是 `Window` 或 `Panel`。

可用控件：`Button`、`TextBox`、`Label`、`CheckBox`、`RadioButton`、`ComboBox`、`ListBox`、`GroupBox`、`Panel`、`Menu`、`Toolbar`、`ButtonBar`、`StatusBar`、`ProgressBar`、`SpinBox`、`Slider`。

### Button

```yxg
Button b = new Button();
b.create(win, "按钮", 10, 10, 100, 30);
if b.is_clicked() != 0 {
    // 被点击（读后清零）
}
```

| 方法 | 说明 |
|---|---|
| `set_text(string)` / `get_text()` | 文字 |
| `is_clicked()` | 点击（读后清零） |
| `is_down()` | 按住 |
| `is_hover()` | 悬停 |
| `has_focus()` | 焦点 |
| `refresh()` | 强制重绘 |
| `set_paint(cb)` | 自绘回调 |
| `destroy()` / `move()` / `set_size()` / `show()` / `hide()` | 通用 |

自绘回调签名：`void cb(int dc, int x, int y, int w, int h, int state)`

```yxg
void myPaint(int dc, int x, int y, int w, int h, int state) {
    int bg = WindowColor.COLOR_GRAY;
    if state & DrawState.STATE_PRESSED != 0 {
        bg = WindowColor.COLOR_BLUE;
    }
    Window.dc_fill_rect(dc, x, y, w, h, bg);
    Window.dc_draw_text(dc, x, y + 8, "自绘", WindowColor.COLOR_WHITE);
}
```

### 其它控件速查

| 控件 | 关键方法 |
|---|---|
| `TextBox` | `set_text` / `get_text` / `clear` / `is_changed` / `is_focused` |
| `Label` | `set_text` / `get_text` / `refresh` / `set_paint` |
| `CheckBox` | `is_checked` / `set_checked` / `is_clicked` |
| `RadioButton` | 同 CheckBox，同父容器下自动互斥 |
| `ComboBox` | `add_item` / `get_selected` / `set_selected` / `get_selected_text` |
| `ListBox` | `add_item` / `get_selected` / `set_selected` / `get_selected_text` |
| `GroupBox` | `create(parent, "标题", ...)` / `set_text` |
| `Panel` | `set_background`，子控件以 Panel 为父 |
| `Menu` | `add_menu` / `add_item` / `add_separator` / `get_clicked` / `popup` |
| `Toolbar` | `create(win, height)` / `add_button` / `get_clicked` |
| `ButtonBar` | `create(win, y)` / `add(btn, text)` |
| `StatusBar` | `set_text` / `get_text` |
| `ProgressBar` | `set_range` / `set_value` / `step` / `set_marquee` |
| `SpinBox` | `set_range` / `set_step` / `set_value` / `get_value` |
| `Slider` | `set_range` / `set_value` / `get_value` / `is_changed` |

### 完整示例：点击计数器

```yxg
import yxg.Window;
import yxg.Components;

Window win;
Button btn;
int count;

pub void main() {
    win = new Window();
    win.create("计数器", 300, 200);
    win.show();
    win.center();

    btn = new Button();
    btn.create(win, "点我", 100, 80, 100, 40);
    count = 0;

    while win.is_valid() != 0 && win.is_key_down(WindowKey.KEY_ESC) == 0 {
        if btn.is_clicked() != 0 {
            count = count + 1;
        }
        win.clear(WindowColor.COLOR_LIGHT_GRAY);
        win.draw_text_center(40, format("次数: {}", count), WindowColor.COLOR_BLACK);
        win.flush();
        win.update();
        win.sleep(16);
    }
    win.close();
}
```

## Music 音频库

导入：`import yxg.Music;`

支持 **WAV / MP3 / FLAC**。

```yxg
Music bgm;

pub void main() {
    bgm = new Music();
    if bgm.load("bgm.mp3") == 1 {
        bgm.set_volume(50);
        bgm.play(1);          // loop=1 循环
    }
}
```

| 方法 | 说明 |
|---|---|
| `load(string path)` | 加载，成功返回 1 |
| `play(int loop)` | 从头播放，loop=1 循环 |
| `pause()` / `resume()` | 暂停 / 恢复（保留位置） |
| `stop()` | 停止并回到起点 |
| `is_playing()` | 是否播放中 |
| `set_volume(int vol)` | 音量 0~100 |
| `get_position()` | 播放位置（毫秒） |
| `unload()` | 释放 |

同一对象 `play` 会从头重播；需要同时多个音效就建多个 `Music` 对象。对象必须全局。

## Video 视频库
：`import yxg.Video;`
导入：`import yxg.Video;`

支持 **MP4/H.264**（系统 Media Foundation 解码，含 AAC 音轨）。

| 方法 | 说明 |
|---|---|
| `load(string path)` | 加载，成功返回 1 |
| `play(int loop)` | 音视频同步播放 |
| `stop()` / `unload()` | 停止 / 释放 |
| `next()` | 解码下一帧：1=新帧，0=结束，-1=错误 |
| `width()` / `height()` | 输出尺寸 |
| `fps()` / `duration()` / `time()` | 帧率 / 时长 / 当前时间 |
| `seek(float sec)` / `rewind()` | 跳转 / 回起点 |
| `set_loop(int)` | 循环开关 |
| `blit_to(Canvas c)` | 当前帧拷到画布（尺寸须一致） |
| `last_error()` | 错误码 |

### 完整播放模板

```yxg
import yxg.Window;
import yxg.Video;

Window win;
Video vid;
Canvas frame;

pub void main() {
    win = new Window();
    win.create("视频", 640, 480);
    win.show();

    vid = new Video();
    if vid.load("test.mp4") == 1 {
        frame = new Canvas();
        frame.create(vid.width(), vid.height());
        vid.play(1);
    } else {
        print "加载失败: ", vid.last_error(), "\n";
        return;
    }

    int t0 = Window.get_ticks();
    while win.is_valid() != 0 && win.is_key_down(WindowKey.KEY_ESC) == 0 {
        int elapsed = Window.get_ticks() - t0;

        // 追帧：连续解码直到追上时间戳
        int guard = 0;
        while vid.time() * 1000.0 <= elapsed && guard < 10 {
            if vid.next() == 0 { break; }
            guard = guard + 1;
        }
        if guard > 0 { vid.blit_to(frame); }

        win.clear(WindowColor.COLOR_BLACK);
        win.draw_canvas_scaled(frame, 0, 0, 640, 480);
        win.flush();
        win.update();
        win.sleep(16);
    }
    win.close();
}
```

### 错误码

| 码 | 含义 |
|---|---|
| 0 | 无错误 |
| 2 | 文件打不开 |
| 3 | 尺寸无效 |
| 6 | 内存分配失败 |
| 7 | 槽位已满（16 个视频对象） |
| 20 | 解码格式不支持 |
| 21 | 输出格式协商失败 |

### 素材转换

用 ffmpeg 转成 H.264 + AAC：

```
ffmpeg -i input.mp4 -vf "scale=1280:720,format=yuv420p" -c:v libx264 -preset medium -crf 23 -c:a aac out720.mp4
```

**输入输出文件名必须不同**，否则源文件会被截断。

## 图形库常见问题

### 窗口一闪就退 / 玩着突然不动

对象被 GC 回收。所有窗口/画布/音频/视频/网络对象必须声明为**全局变量**。

### 鼠标移出后控件颜色不恢复

只在 `is_hover()` 为真时刷新。改成边沿检测：

```yxg
int lastHover = -1;
int h = btn.is_hover();
if h != lastHover {
    btn.refresh();
    lastHover = h;
}
```

### 图片/音频/视频加载失败

1. 文件在 exe 运行目录吗？
2. 格式支持吗？（图片 BMP/PNG/JPG；音频 WAV/MP3/FLAC；视频 H.264 MP4）
3. 视频错误码：2=文件找不到，20=格式不支持

### 编译后留下 .obj 文件

`image_loader.obj`、`music_loader.obj`、`video_loader.obj` 是编译缓存，正常现象。改了对应 `.c` 文件后要删掉 `.obj` 才会重编。

### 改完代码但行为没变

obj 缓存没删。改任何 `.c` 库文件后必须删对应 `.obj`。

## 继续阅读

下一章介绍网络与文件库。
