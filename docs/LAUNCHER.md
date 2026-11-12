# 首页导航 / Home navigation

在“系统设置 → 首页导航”中选择一个入口，再点“上移”“下移”“替换”或“隐藏”。“添加入口”可把已隐藏的功能放回首页；“恢复默认”恢复书架、家庭、小游戏的顺序。打印机也可作为独立首页。

Choose a destination in Settings → Home Navigation, then move it up/down, replace or hide it. Add Destination restores a hidden function. Restore Defaults returns to Books, Home and Games. Printers can be a separate home destination.

首页最多显示五个入口，至少保留一个可用入口；设置按钮独立保留。顶部按钮和中键双击打开的页面切换器使用同一份顺序；左右滑动在书架、家庭、应用集和游戏目录中翻动内容页，不切换栏目。长按中键仍直接回书架，快捷菜单中的书架、家庭和小游戏入口仍直接到对应功能。

Keep one to five available destinations. Settings remains separately accessible. Top buttons and the double-press page switcher share the saved order. Horizontal swipes paginate shelf, Home, Apps and game content instead of switching destinations. Holding the middle key returns directly to Books; named corner shortcuts retain their fixed destinations.

点击“完成”才保存并生效；“取消”或返回会丢弃本次调整。保存失败保留草稿，可以重试。选择浮层可点击外部空白或返回关闭，关闭动作不会点中下面的页面。较长的应用名称在自己的导航格内截短，不缩小字号。

Done persists and applies the draft. Cancel or Back discards it. Failed persistence keeps the draft for retry. Dismiss the destination picker by tapping outside or pressing Back without clicking through. Long application labels truncate within their own navigation cells at a fixed font size.

## 应用注册契约 / App registration contract

`main/system/pico_navigation.h` 提供共享注册表。书架、家庭、小游戏和打印机的持久 ID 分别为 `1`、`2`、`3`、`4`，不依赖路由枚举顺序。自定义应用使用固定且唯一的 `256` 以上 ID，在 `ps_navigation_init()` 后通过 `ps_navigation_register()` 注册。最多注册十二个候选入口，注册本身不自动添加到首页。

The shared registry uses stable IDs independent of route enum order: Books `1`, Home `2`, Games `3`, Printers `4`. Register custom apps after `ps_navigation_init()` with unique stable IDs starting at `256`, using `ps_navigation_register()`. Up to twelve destinations may be registered; registration alone does not add a home slot.

名称和可选启动回调必须在注册表使用期间有效。外部应用使用 `PS_ROUTE_COUNT` 加启动回调，内置页面可指定有效路由；已有路由不接受无回调的重复别名。应用卸载后未注册的已存 ID 暂不显示，重新注册可恢复原位置；全部不可用时提供书架作为恢复入口。此注册接口是后续自定义应用的接入点，不代表本次已实现任意第三方应用执行。

Labels and optional launch callbacks must remain valid for the registry lifetime. External apps supply `PS_ROUTE_COUNT` plus a launcher; built-in pages may supply a valid unique route. Saved but unregistered app IDs stay dormant and recover their position when registered again. Books is the recovery destination if every saved app is unavailable. The registry enables future app integration; it does not itself execute arbitrary third-party apps.

导航保存在共享存储的 `navigation1` 记录中，沿用带校验的事务保存；存储维护迁移只保存正在使用的导航，不提交编排草稿。不要用显示名称或列表位置作为应用身份。

The shared `navigation1` record uses checksummed transactional persistence. Storage migration saves live navigation without committing an editor draft. Never use a display name or list position as app identity.

## 验证 / Validation

`python3 tools/test_pico_navigation.py` 检查稳定 ID、重启恢复、排序替换、隐藏边界、保存失败、草稿隔离、动态注册及双方向绘制范围。`python3 tools/test_pico_system.py` 检查顶部栏目顺序、打印机根页、返回来源和固定快捷动作。`tools/render_pico_navigation.py` 与 `tools/render_pico_system.py` 输出同源字体截图；不替代实体屏幕和触摸验收。

The navigation host test covers stable IDs, restart, ordering/replacement, hide limits, save failure, draft isolation, registration and two-orientation bounds. The shell host test checks top destination order, printer roots, return origin and fixed shortcut destinations. The navigation/system renderers produce same-source font screenshots; physical display and touch acceptance remains separate.

## 快捷菜单与共用导航 / Quick menus and shared navigation

“系统设置 → 外观与操作 → 快捷菜单”提供“显示指示标识”开关及功能编排。隐藏标识不关闭滑动功能。底角点击、长按和小幅抖动不打开菜单；从底角向内并向上滑动才接管触摸。临时提示统一覆盖顶部状态栏，结束后恢复原状态，不遮盖正文和页脚。

Settings → Appearance and Interaction → Quick Menu provides indicator visibility and shortcut editing. Hidden indicators retain the gesture. Corner taps, holds and jitter do not open the menu; a deliberate inward/upward swipe claims it. Transient notices replace the top bar and restore it on expiry without covering book body or footer.

返回按钮采用圆角矩形，外框与页面内容共用16逻辑单位边距，图标与标题垂直居中。家庭连接保留左上角唯一返回入口。书架“书架样式”浮层同时显示样式及排序，两项选择分别保存。

Rounded Back controls share the content’s 16-unit margin and center icons with headings. Home Link keeps one upper-left Back control. Shelf Style shows both layout and sort choices with independent persistence.
