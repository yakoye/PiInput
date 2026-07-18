# 中文输入法测试语料 v0.2.0

本版重点验证长句切分、上下文消歧、专业词库、容错纠错、模糊音和用户词频学习。

## 1. 小鹤双拼基础词

| 目标文本 | 拼音音节 | 双拼分码 | 连续输入 |
|---|---|---|---|
| 的 | `de` | `de` | `de` |
| 是 | `shi` | `ui` | `ui` |
| 在 | `zai` | `zd` | `zd` |
| 输入法 | `shu ru fa` | `uu ru fa` | `uurufa` |
| 候选窗口 | `hou xuan chuang kou` | `hz xr il kz` | `hzxrilkz` |
| 拼音方案 | `pin yin fang an` | `pb yb fh an` | `pbybfhan` |
| 中文字符 | `zhong wen zi fu` | `vs wf zi fu` | `vswfzifu` |
| 用户体验 | `yong hu ti yan` | `ys hu ti yj` | `yshutiyj` |
| 软件开发 | `ruan jian kai fa` | `rr jm kd fa` | `rrjmkdfa` |
| 数据结构 | `shu ju jie gou` | `uu ju jp gz` | `uujujpgz` |
| 人工智能 | `ren gong zhi neng` | `rf gs vi ng` | `rfgsving` |
| 操作系统 | `cao zuo xi tong` | `cc zo xi ts` | `cczoxits` |
| 阿 | `a` | `aa` | `aa` |
| 哦 | `o` | `oo` | `oo` |
| 额 | `e` | `ee` | `ee` |
| 昂 | `ang` | `ah` | `ah` |
| 恩 | `en` | `en` | `en` |
| 欧 | `ou` | `ou` | `ou` |
| 窗 | `chuang` | `il` | `il` |
| 双 | `shuang` | `ul` | `ul` |
| 强 | `qiang` | `ql` | `ql` |
| 我今天下午要去超市买点水果。 | `wo jin tian xia wu yao qu chao shi mai dian shui guo` | `wo jb tm xx wu yc qu ic ui md dm uv go` | `wojbtmxxwuycquicuimddmuvgo` |
| 今天我们开始测试中文输入法。 | `jin tian wo men kai shi ce shi zhong wen shu ru fa` | `jb tm wo mf kd ui ce ui vs wf uu ru fa` | `jbtmwomfkduiceuivswfuurufa` |
| 固件开发需要熟悉底层寄存器配置和链路状态机。 | `gu jian kai fa xu yao shu xi di ceng ji cun qi pei zhi he lian lu zhuang tai ji` | `gu jm kd fa xu yc uu xi di cg ji cy qi pw vi he lm lu vl td ji` | `gujmkdfaxuycuuxidicgjicyqipwvihelmluvltdji` |
| 链路训练需要检查状态机和均衡过程。 | `lian lu xun lian xu yao jian cha zhuang tai ji he jun heng guo cheng` | `lm lu xy lm xu yc jm ia vl td ji he jy hg go ig` | `lmluxylmxuycjmiavltdjihejyhggoig` |

## 2. 长句切分与候选质量

### LONG-DAILY-001

目标：我今天下午要去超市买点水果。

全拼：`wojintianxiawuyaoquchaoshimaidianshuiguo`

音节：`wo jin tian xia wu yao qu chao shi mai dian shui guo`

测试点：长句切分、天下/今天下午边界、超市、水果、整句首候选

### LONG-IME-001

目标：今天我们开始测试中文输入法。

全拼：`jintianwomenkaishiceshizhongwenshurufa`

音节：`jin tian wo men kai shi ce shi zhong wen shu ru fa`

测试点：连续输入、中文输入法词组命中、候选首选

### LONG-FW-001

目标：固件开发需要熟悉底层寄存器配置和链路状态机。

全拼：`gujiankaifaxuyaoshuxidicengjicunqipeizhihelianluzhuangtaiji`

音节：`gu jian kai fa xu yao shu xi di ceng ji cun qi pei zhi he lian lu zhuang tai ji`

测试点：专业词库、固件、寄存器、配置、链路状态机

### LONG-PCIE-001

目标：链路训练需要检查状态机和均衡过程。

全拼：`lianluxunlianxuyaojianchazhuangtaijihejunhengguocheng`

音节：`lian lu xun lian xu yao jian cha zhuang tai ji he jun heng guo cheng`

测试点：链路训练、状态机、均衡过程、专业上下文

### LONG-IME-002

目标：明天上午我们一起检查输入法候选排序是否正确。

全拼：`mingtianshangwuwomenyiqijianchashurufahouxuanpaixushifouzhengque`

音节：`ming tian shang wu wo men yi qi jian cha shu ru fa hou xuan pai xu shi fou zheng que`

测试点：长句切分、候选排序、是否/事实边界、整句首候选

### LONG-IME-003

目标：用户连续输入长句时引擎需要正确切分拼音音节。

全拼：`yonghulianxushuruchangjushiyinqingxuyaozhengqueqiefenpinyinyinjie`

音节：`yong hu lian xu shu ru chang ju shi yin qing xu yao zheng que qie fen pin yin yin jie`

测试点：连续输入、音节切分、引擎、拼音音节

### LONG-IME-004

目标：删除一个字母以后候选窗口应该立即刷新。

全拼：`shanchuyigezimuyihouhouxuanchuangkouyinggailijishuaxin`

音节：`shan chu yi ge zi mu yi hou hou xuan chuang kou ying gai li ji shua xin`

测试点：组合串编辑、候选刷新、重复 hou 边界

### LONG-IME-005

目标：同一个拼音在不同上下文中可能对应不同词语。

全拼：`tongyigepinyinzaibutongshangxiawenzhongkenengduiyingbutongciyu`

音节：`tong yi ge pin yin zai bu tong shang xia wen zhong ke neng dui ying bu tong ci yu`

测试点：上下文消歧、同音词、重复词组

### LONG-IME-006

目标：输入法应该记住用户经常选择的专业词汇。

全拼：`shurufayinggaijizhuyonghujingchangxuanzedezhuanyecihui`

音节：`shu ru fa ying gai ji zhu yong hu jing chang xuan ze de zhuan ye ci hui`

测试点：用户学习、专业词汇、候选排序

### LONG-FUZZY-001

目标：关闭模糊音以后输入结果必须严格区分前后鼻音。

全拼：`guanbimohuyinyihoushurujieguobixuyangequfenqianhoubiyin`

音节：`guan bi mo hu yin yi hou shu ru jie guo bi xu yan ge qu fen qian hou bi yin`

测试点：模糊音开关、前后鼻音、严格模式

### LONG-FW-002

目标：固件需要读取配置空间并处理链路状态变化。

全拼：`gujianxuyaoduqupeizhikongjianbingchulilianluzhuangtaibianhua`

音节：`gu jian xu yao du qu pei zhi kong jian bing chu li lian lu zhuang tai bian hua`

测试点：固件、配置空间、链路状态、专业词库

### LONG-PCIE-002

目标：设备进入恢复状态以后需要重新完成链路训练。

全拼：`shebeijinruhuifuzhuangtaiyihouxuyaochongxinwanchenglianluxunlian`

音节：`she bei jin ru hui fu zhuang tai yi hou xu yao chong xin wan cheng lian lu xun lian`

测试点：恢复状态、链路训练、专业上下文

### LONG-DRIVER-001

目标：驱动程序通过内存映射访问设备寄存器。

全拼：`qudongchengxutongguoneicunyingshefangwenshebeijicunqi`

音节：`qu dong cheng xu tong guo nei cun ying she fang wen she bei ji cun qi`

测试点：驱动程序、内存映射、设备寄存器

### LONG-PCIE-003

目标：数据链路层负责可靠传输和错误恢复。

全拼：`shujulianlucengfuzekekaochuanshuhecuowuhuifu`

音节：`shu ju lian lu ceng fu ze ke kao chuan shu he cuo wu hui fu`

测试点：数据链路层、可靠传输、错误恢复

### LONG-OS-001

目标：操作系统需要正确保存并恢复输入法状态。

全拼：`caozuoxitongxuyaozhengquebaocunbinghuifushurufazhuangtai`

音节：`cao zuo xi tong xu yao zheng que bao cun bing hui fu shu ru fa zhuang tai`

测试点：操作系统、状态恢复、输入法状态

### LONG-OS-002

目标：线程切换不能导致组合字符串丢失或重复上屏。

全拼：`xianchengqiehuanbunengdaozhizuhezifuchuandiushihuochongfushangping`

音节：`xian cheng qie huan bu neng dao zhi zu he zi fu chuan diu shi huo chong fu shang ping`

测试点：线程切换、组合字符串、丢失、重复上屏

### LONG-REPORT-001

目标：这个文件包含完整的测试用例和评估指标。

全拼：`zhegewenjianbaohanwanzhengdeceshiyonglihepingguzhibiao`

音节：`zhe ge wen jian bao han wan zheng de ce shi yong li he ping gu zhi biao`

测试点：文件/稳健消歧、测试用例、评估指标

### LONG-BUILD-001

目标：编译完成以后请检查日志中的错误信息。

全拼：`bianyiwanchengyihouqingjiancharizhizhongdecuowuxinxi`

音节：`bian yi wan cheng yi hou qing jian cha ri zhi zhong de cuo wu xin xi`

测试点：编译/便宜消歧、日志、错误信息

### LONG-OS-003

目标：系统发生中断以后终端仍然可以继续输入。

全拼：`xitongfashengzhongduanyihouzhongduanrengrankeyijixushuru`

音节：`xi tong fa sheng zhong duan yi hou zhong duan reng ran ke yi ji xu shu ru`

测试点：中断/终端同音消歧、重复同音词、上下文

### LONG-REPORT-002

目标：测试报告需要记录目标候选的排名和响应延迟。

全拼：`ceshibaogaoxuyaojilumubiaohouxuandepaiminghexiangyingyanchi`

音节：`ce shi bao gao xu yao ji lu mu biao hou xuan de pai ming he xiang ying yan chi`

测试点：候选排名、响应延迟、测试报告

## 3. 上下文消歧对照组

同一组必须连续测试，比较上下文改变后目标词的排名。

| 用例 | 歧义编码 | 目标词 | 对照词 | 目标句 |
|---|---|---|---|---|
| CTX-GUJIAN-A | `gujian` | 固件 | 古剑 | 设备固件需要及时升级。 |
| CTX-GUJIAN-B | `gujian` | 古剑 | 固件 | 他收藏了一把古剑。 |
| CTX-ZHONGDUAN-A | `zhongduan` | 中断 | 终端 | 系统发生中断。 |
| CTX-ZHONGDUAN-B | `zhongduan` | 终端 | 中断 | 请打开命令行终端。 |
| CTX-DIZHI-A | `dizhi` | 地址 | 地质 | 请检查设备地址。 |
| CTX-DIZHI-B | `dizhi` | 地质 | 地址 | 他正在研究地质结构。 |
| CTX-WENJIAN-A | `wenjian` | 文件 | 稳健 | 请打开这个文件。 |
| CTX-WENJIAN-B | `wenjian` | 稳健 | 文件 | 这个方案更加稳健。 |
| CTX-XIANCHENG-A | `xiancheng` | 线程 | 县城 | 程序创建了一个线程。 |
| CTX-XIANCHENG-B | `xiancheng` | 县城 | 线程 | 他们住在一个安静的县城。 |
| CTX-YICHANG-A | `yichang` | 异常 | 一场 | 系统出现异常。 |
| CTX-YICHANG-B | `yichang` | 一场 | 异常 | 下午有一场比赛。 |
| CTX-BIANYI-A | `bianyi` | 编译 | 便宜 | 需要重新编译程序。 |
| CTX-BIANYI-B | `bianyi` | 便宜 | 编译 | 这件商品比较便宜。 |
| CTX-XIEYI-A | `xieyi` | 协议 | 谢意 | 双方遵循通信协议。 |
| CTX-XIEYI-B | `xieyi` | 谢意 | 协议 | 他向大家表达谢意。 |
| CTX-SHIWU-A | `shiwu` | 事务 | 食物 | 数据库正在提交事务。 |
| CTX-SHIWU-B | `shiwu` | 食物 | 事务 | 冰箱里还有一些食物。 |
| CTX-SHUJU-A | `shuju` | 数据 | 数句 | 请分析这些数据。 |
| CTX-SHUJU-B | `shuju` | 数句 | 数据 | 他朗读了数句古诗。 |
| CTX-HUIFU-A | `huifu` | 恢复 | 回复 | 系统已经恢复正常。 |
| CTX-HUIFU-B | `huifu` | 回复 | 恢复 | 请尽快回复邮件。 |
| CTX-QIANYI-A | `qianyi` | 迁移 | 千亿 | 我们正在完成数据迁移。 |
| CTX-QIANYI-B | `qianyi` | 千亿 | 迁移 | 市场规模已经达到千亿。 |
| CTX-HEXIN-A | `hexin` | 核心 | 河心 | 这是系统的核心模块。 |
| CTX-HEXIN-B | `hexin` | 河心 | 核心 | 小船慢慢驶向河心。 |
| CTX-QUANLI-A | `quanli` | 权利 | 权力 | 公民依法享有合法权利。 |
| CTX-QUANLI-B | `quanli` | 权力 | 权利 | 管理人员不能滥用权力。 |
| CTX-SHISHI-A | `shishi` | 事实 | 实时 | 这是一项客观事实。 |
| CTX-SHISHI-B | `shishi` | 实时 | 事实 | 系统正在实时更新。 |
| CTX-XINGSHI-A | `xingshi` | 形式 | 行事 | 会议采用线上形式。 |
| CTX-XINGSHI-B | `xingshi` | 行事 | 形式 | 他一向谨慎行事。 |
| CTX-JISHI-A | `jishi` | 即时 | 及时 | 消息需要即时同步。 |
| CTX-JISHI-B | `jishi` | 及时 | 即时 | 发现问题以后及时处理。 |
| CTX-GONGSHI-A | `gongshi` | 公式 | 公示 | 请使用这个计算公式。 |
| CTX-GONGSHI-B | `gongshi` | 公示 | 公式 | 结果将在明天公示。 |
| CTX-SHIYAN-A | `shiyan` | 实验 | 食盐 | 我们准备进行实验。 |
| CTX-SHIYAN-B | `shiyan` | 食盐 | 实验 | 厨房里需要一些食盐。 |
| CTX-SHUZHI-A | `shuzhi` | 数值 | 树脂 | 请读取寄存器数值。 |
| CTX-SHUZHI-B | `shuzhi` | 树脂 | 数值 | 这个零件使用树脂材料。 |
| CTX-JIEGUO-A | `jieguo` | 结果 | 借过 | 测试结果符合预期。 |
| CTX-JIEGUO-B | `jieguo` | 借过 | 结果 | 麻烦借过一下。 |
| CTX-GUANJIAN-A | `guanjian` | 关键 | 管见 | 这是一个关键问题。 |
| CTX-GUANJIAN-B | `guanjian` | 管见 | 关键 | 以上只是个人管见。 |
| CTX-XIANGMU-A | `xiangmu` | 项目 | 橡木 | 这个项目已经完成。 |
| CTX-XIANGMU-B | `xiangmu` | 橡木 | 项目 | 桌子使用橡木制作。 |
| CTX-CESHI-A | `ceshi` | 测试 | 侧室 | 现在开始进行测试。 |
| CTX-CESHI-B | `ceshi` | 侧室 | 测试 | 古代宅院里设有侧室。 |

## 4. 切分歧义

| 输入 | 目标 | 强制切分 | 上下文 |
|---|---|---|---|
| `xian` | 西安 | `xi'an` | 我准备明天去西安。 |
| `fangan` | 方案 | `fang'an` | 请确认最终方案。 |
| `pingan` | 平安 | `ping'an` | 祝你一路平安。 |
| `changan` | 长安 | `chang'an` | 长安是历史地名。 |
| `tiananmen` | 天安门 | `tian'an'men` | 游客来到天安门。 |
| `jiefang` | 解放 | `-` | 这座城市迎来解放。 |
| `jiefang` | 街坊 | `-` | 老街坊正在门口聊天。 |
| `shangan` | 上岸 | `shang'an` | 游泳的人已经上岸。 |
| `jintianxiawu` | 今天下午 | `-` | 我今天下午去开会。 |
| `shijian` | 时间 | `-` | 请确认会议时间。 |
| `shijian` | 事件 | `-` | 系统记录了异常事件。 |
| `gongshi` | 公式 | `-` | 请检查计算公式。 |
| `gongshi` | 公示 | `-` | 结果将在网站公示。 |
| `shuzhi` | 数值 | `-` | 寄存器数值已经更新。 |
| `jieguo` | 结果 | `-` | 测试结果符合预期。 |
| `xiangmu` | 项目 | `-` | 这个项目已经完成。 |

## 5. 专业词库

### PCIe/Firmware

- `gujian` → 固件
- `jicunqi` → 寄存器
- `peizhikongjian` → 配置空间
- `lianluzhuangtaiji` → 链路状态机
- `lianluxunlian` → 链路训练
- `shiwuceng` → 事务层
- `shujulianluceng` → 数据链路层
- `wuliceng` → 物理层
- `shiwucengshujubao` → 事务层数据包
- `peizhiduqingqiu` → 配置读请求
- `peizhixieqingqiu` → 配置写请求
- `neicunduqingqiu` → 内存读请求
- `neicunxieqingqiu` → 内存写请求
- `wanchengbaowen` → 完成报文
- `wanchengchaoshi` → 完成超时
- `ketiaozhengjidizhijicunqi` → 可调整基地址寄存器
- `gaojicuowubaogao` → 高级错误报告
- `lianlujunheng` → 链路均衡
- `jieshouduan` → 接收端
- `fasongduan` → 发送端
- `lianlukuandu` → 链路宽度
- `lianlusulv` → 链路速率
- `liuliangkongzhi` → 流量控制
- `xinyongedu` → 信用额度
- `zhongduanqingqiu` → 中断请求
- `xiaoxixinhaozhongduan` → 消息信号中断
- `shurushuchuneicunguanlidanyuan` → 输入输出内存管理单元
- `dizhizhuanhuanfuwu` → 地址转换服务
- `jinchengdizhikongjianbiaozhifu` → 进程地址空间标识符
- `dangenshurushuchuxunihua` → 单根输入输出虚拟化

### Software/OS

- `caozuoxitong` → 操作系统
- `shebeiqudong` → 设备驱动
- `neicunyingshe` → 内存映射
- `xunidizhi` → 虚拟地址
- `wulidizhi` → 物理地址
- `yebiao` → 页表
- `zhongduanchuli` → 中断处理
- `duoxiancheng` → 多线程
- `huchisuo` → 互斥锁
- `tiaojianbianliang` → 条件变量
- `shijianxunhuan` → 事件循环
- `zhuangtaiji` → 状态机
- `huancunyizhixing` → 缓存一致性
- `minglinghangzhongduan` → 命令行终端
- `bianyiqi` → 编译器
- `lianjieqi` → 链接器
- `tiaoshiqi` → 调试器
- `rizhixitong` → 日志系统
- `cuowuhuifu` → 错误恢复

### Input Method

- `houxuanchuangkou` → 候选窗口
- `houxuanpaixu` → 候选排序
- `pinyinqiefen` → 拼音切分
- `yonghucipin` → 用户词频
- `mohuyin` → 模糊音
- `lingshengmu` → 零声母
- `geyinfu` → 隔音符
- `zuhezifuchuan` → 组合字符串
- `wenbentijiao` → 文本提交
- `guangbiaoweizhi` → 光标位置

## 6. 容错纠错抽样

完整 160 条结构化用例见 `tests/correction_test_cases.json`。关闭纠错时要求行为稳定；开启纠错时记录目标候选排名，不强制所有错码都必须首选。

| 方案 | 类型 | 正确输入 | 错误输入 | 目标 |
|---|---|---|---|---|
| full_pinyin | adjacent_replacement | `nihao` | `hihao` | 你好 |
| full_pinyin | missing_key | `nihao` | `niao` | 你好 |
| full_pinyin | repeated_key | `nihao` | `nihhao` | 你好 |
| full_pinyin | transposed_keys | `nihao` | `nhiao` | 你好 |
| full_pinyin | adjacent_replacement | `pingguo` | `oingguo` | 苹果 |
| full_pinyin | missing_key | `pingguo` | `pinguo` | 苹果 |
| full_pinyin | repeated_key | `pingguo` | `pinggguo` | 苹果 |
| full_pinyin | transposed_keys | `pingguo` | `pignguo` | 苹果 |
| full_pinyin | adjacent_replacement | `shiji` | `whiji` | 实际 |
| full_pinyin | missing_key | `shiji` | `shji` | 实际 |
| full_pinyin | repeated_key | `shiji` | `shiiji` | 实际 |
| full_pinyin | transposed_keys | `shiji` | `sihji` | 实际 |
| full_pinyin | adjacent_replacement | `zhongguo` | `ahongguo` | 中国 |
| full_pinyin | missing_key | `zhongguo` | `zhonguo` | 中国 |
| full_pinyin | repeated_key | `zhongguo` | `zhonggguo` | 中国 |
| full_pinyin | transposed_keys | `zhongguo` | `zhognguo` | 中国 |
| full_pinyin | adjacent_replacement | `wojintianxiawu` | `qojintianxiawu` | 我今天下午 |
| full_pinyin | missing_key | `wojintianxiawu` | `wojintinxiawu` | 我今天下午 |
| full_pinyin | repeated_key | `wojintianxiawu` | `wojintiaanxiawu` | 我今天下午 |
| full_pinyin | transposed_keys | `wojintianxiawu` | `wojintainxiawu` | 我今天下午 |
| full_pinyin | adjacent_replacement | `shurufa` | `whurufa` | 输入法 |
| full_pinyin | missing_key | `shurufa` | `shuufa` | 输入法 |
| full_pinyin | repeated_key | `shurufa` | `shurrufa` | 输入法 |
| full_pinyin | transposed_keys | `shurufa` | `shruufa` | 输入法 |
| full_pinyin | adjacent_replacement | `houxuanchuangkou` | `youxuanchuangkou` | 候选窗口 |
| full_pinyin | missing_key | `houxuanchuangkou` | `houxuancuangkou` | 候选窗口 |
| full_pinyin | repeated_key | `houxuanchuangkou` | `houxuanchhuangkou` | 候选窗口 |
| full_pinyin | transposed_keys | `houxuanchuangkou` | `houxuanhcuangkou` | 候选窗口 |
| full_pinyin | adjacent_replacement | `pinyinfangan` | `oinyinfangan` | 拼音方案 |
| full_pinyin | missing_key | `pinyinfangan` | `pinyinangan` | 拼音方案 |
| full_pinyin | repeated_key | `pinyinfangan` | `pinyinffangan` | 拼音方案 |
| full_pinyin | transposed_keys | `pinyinfangan` | `pinyifnangan` | 拼音方案 |

## 7. 模糊音开关

| 模糊音 | 状态 | 测试输入 | 正确输入 | 目标 |
|---|---|---|---|---|
| z/zh | 关 | `zongwen` | `zhongwen` | 中文 |
| z/zh | 开 | `zongwen` | `zhongwen` | 中文 |
| c/ch | 关 | `cengxu` | `chengxu` | 程序 |
| c/ch | 开 | `cengxu` | `chengxu` | 程序 |
| s/sh | 关 | `sishi` | `shishi` | 事实 |
| s/sh | 开 | `sishi` | `shishi` | 事实 |
| n/l | 关 | `nanse` | `lanse` | 蓝色 |
| n/l | 开 | `nanse` | `lanse` | 蓝色 |
| f/h | 关 | `hangfa` | `fangfa` | 方法 |
| f/h | 开 | `hangfa` | `fangfa` | 方法 |
| an/ang | 关 | `angquan` | `anquan` | 安全 |
| an/ang | 开 | `angquan` | `anquan` | 安全 |
| en/eng | 关 | `zhenchang` | `zhengchang` | 正常 |
| en/eng | 开 | `zhenchang` | `zhengchang` | 正常 |
| in/ing | 关 | `xinqi` | `xingqi` | 星期 |
| in/ing | 开 | `xinqi` | `xingqi` | 星期 |
| ian/iang | 关 | `jiangkang` | `jiankang` | 健康 |
| ian/iang | 开 | `jiangkang` | `jiankang` | 健康 |
| uan/uang | 关 | `guangbi` | `guanbi` | 关闭 |
| uan/uang | 开 | `guangbi` | `guanbi` | 关闭 |

## 8. 用户学习

按 `tests/user_learning_test_cases.json` 执行：记录初始排名、重复选择、重启、删除用户词、恢复默认、密码框与隐私模式。

## 9. 字符与混合输入

```text
中文 English 混合输入 2026。
PCIe 6.0、GPU、CPU 和 AI 芯片。
yakoye.github.io
user@example.com
C:\Users\color\Documents
/Users/name/Documents
🙂 😂 👍 🚀 ❤️ 👍🏻 👨‍👩‍👧‍👦
龘 鱻 麤 靐 齉 爨 彧 翀 昉 玥 𠮷 𠀀
```

## 10. 非打印按键

请使用 `keyboard/manual_key_checklist.md` 和状态机 JSON；静态文本无法验证 Backspace、Delete、方向键、Esc、Enter 和修饰键。
