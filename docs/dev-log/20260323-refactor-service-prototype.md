Git Commit Message:
refactor(client): 修复服务监控原型页异常操作表列错位

Modification:
- client/src/views/ServiceMonitorPrototype.vue
- docs/dev-log/20260323-refactor-service-prototype.md

Learning Tips:

Newbie Tips:
- 改表格列数时，表头和表体必须一起改。既然表头已经从 4 列裁成 3 列，那么 `grid-cols` 和每一行的单元格数量也要同步收掉，不然页面就会直接错位。

Function Explanation:
- `grid-cols-[2fr_1fr_1fr]`
  这里是 Tailwind 的任意值网格写法，表示第一列更宽，后两列等宽。既然异常操作表现在只保留“操作 / 异常链路数 / 异常平均耗时”三列，那么表头和表体都必须用这一套列模板。

Pitfalls:
- 只删表头、不删表体，是最常见的 UI 半改状态。页面看起来像改了，其实 DOM 结构已经不一致了，后面再调样式只会越修越乱。
