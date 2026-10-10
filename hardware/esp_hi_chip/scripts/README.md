# 生成与复核脚本

最终可编辑工程在 `../native/`。这些脚本保留设计过程与检查能力；重新生成电路或重新布线是一次新的工程修订，不会自动继承当前 PASS 状态。

Python 使用 `numpy`、`scipy`、`shapely`、`numba`、`matplotlib`；导出复核使用 `gerbonara==1.6.3`、`PyMuPDF==1.28.2`。脚本优先使用正常 Python 环境，兼容本机仓库下 `.local/pcb-python` 的隔离依赖。立创调用使用官方专业版 4.1.71 CLI，无需账号激活文件进入仓库。

## 只读/报告检查

在仓库根目录：

```powershell
python hardware/esp_hi_chip/scripts/check_geometry.py
python hardware/esp_hi_chip/scripts/check_manufacturing.py
python hardware/esp_hi_chip/scripts/check_exports.py hardware/esp_hi_chip/exports/ESP-HI-C3-RevA-prototype-gerber.zip
python hardware/esp_hi_chip/scripts/render_schematic.py
python hardware/esp_hi_chip/scripts/render_board.py
python hardware/esp_hi_chip/scripts/check_native_contract.py
```

上述脚本会更新检查 JSON 或预览文件，不修改原生 CAD。Gerbonara 对立创 Excellon 的 `G90` 头部位置给出语法提醒，但解析到的绝对毫米孔位已经逐孔核对；该提醒不被隐藏。

原生验证用立创 CLI `invoke --ext-uuid eda --code-file ...`，先打开对应的 PCB 文档，调用 `eda.pcb_Drc.check(true,false,true)`；原理图用 `eda.sch_Drc.check(true,false,true)`。`export_pro.js` 的输出路径需改成本机工程目录，仅导出文件，不提交订单。

## 生成链路和已知边界

1. `build_model.py` 生成 `design-intent.json`、初始 BOM 和引脚合同。它生成的是待复核初稿。
2. `generate_cad.py --keep-placement` 生成 Standard 中间格式与封装；`cad/placement.json` 为最终布局位置。`design-intent.json` 中每个器件的 `pcb_mm` 仅是最初布局建议。
3. `build_routing.py`、`finish_routes.py`、`route_iterations.py` 完成关键走线、网格布线和迭代；`ground_iterations.py`、`pour_ground.py`、`repair_geometry.py` 处理接地及精确间距；`widen_power.py` 加宽允许范围内的电源线。
4. `export_native_pcb.py` 是历史 Standard 路由导出入口。旧缓存铺铜与 Pro 的表示不同，不能拿缓存铺铜直接制造。
5. Standard 工程导入 Pro 后需要建立原理图/PCB 关联。`update_pro_geometry.py` 更新关闭的原生文件中的铜；`finalize_pro_footprints.py` 更新封装/装配层/可见器件值。**编辑原生文件前关闭相应文档，之后在 Pro 内重新铺铜、保存并执行原生 DRC。**
6. `check_native_contract.py` 比较原生导出的 Protel2 网络表、检查模型和原生铜几何；保留最终导出作为可复核输入。
7. `check_exports.py` 解析真正的原生 Gerber，复核板框、钻孔和底面开窗，再制作去重后的加工 ZIP。输出文件 SHA-256 记录在报告中。

网格布线不是电气仿真；即使所有脚本 PASS，也不能推断射频、USB 信号完整性、音频噪声、散热和屏幕折弯已经通过。相应样板测试在 `../VALIDATION.md` 单独列明。

`libraries/` 中来源于 KiCad 的封装保留其 CC BY-SA 4.0 与设计使用例外声明。第三方厂家 PDF 不随仓库重复发布，下载地址和所用版本哈希见 `../SOURCES.md`。
