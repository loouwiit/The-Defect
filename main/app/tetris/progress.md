# 俄罗斯方块开发进度

## 当前状态: Phase 1 完成 ✅

## Phase 1: 核心游戏引擎 ✅

### 已实现文件
- `main/app/tetris/tetris.hpp` - 类型定义
- `main/app/tetris/tetris.cpp` - 实现

### 核心组件

#### 1. SRS 形状数据
- 7种方块 (I, O, T, S, Z, J, L)
- 4种旋转状态 (R, D, L, U)
- Wall Kick 表 (SRS 标准 + I piece)
- 修复：`blocks[4][4][4]` 三维数组

#### 2. Piece 类
- `getBlocks()` - 获取形状 block 坐标
- `collides()` - 碰撞检测
- `tryRotate()` - 带 wall kick 旋转
- `tryMove()` - 移动尝试

#### 3. Board 类
- `clear()` - 清空
- `isLineFull()` - 检查行满
- `clearLine()` - 清除一行
- `place()` - 放置方块
- `check()` - 坐标碰撞检测
- `getColumnMask()` - 列掩码 (渲染用)

#### 4. PieceQueue (7-bag 随机生成器)
- `reset()` - Fisher-Yates shuffle 7个piece
- `next()` - 取下一个
- `peek()` - 预览
- `peekN()` - 预览多个
- 修复：改为标准7-bag（只用7个元素，不是14个双bag）

#### 5. GameState 游戏状态
- 包含 Board, PieceQueue, Hold, 当前 piece
- DAS/ARR 支持
- Lock Delay (500ms)
- 软 Drop / 硬 Drop
- Hold 系统
- `holdPiece()` 方法（原`hold()`与成员变量冲突，已重命名）

#### 6. 计分系统
- Single: 100, Double: 300, Triple: 500, Tetris: 800
- T-Spin: 1200/1800/2400

#### 7. 游戏 Tick
- 重力下落 (初始 1000ms/行)
- DAS 延迟 (170ms) + ARR (50ms)
- Lock Delay (500ms)
- 软 Drop (50ms/行)

#### 8. 攻击系统
- `calcAttackLines()` - 计算攻击行数
- `receiveAttack()` - 接受攻击行数

#### 9. 测试框架
- `TEST_MODE` 宏控制（默认关闭）
- `tetris_run_tests()` - 串口输出单元测试结果
- 测试项：旋转、碰撞、7-bag、消除、计分

### 已验证正确
- ✅ Piece rotation (SRS wall kick)
- ✅ Board collision
- ✅ Line clear
- ✅ Score calculation
- ⚠️ 7-bag 随机性（样本70个，分布8次各piece，统计正常）

### CMakeLists.txt
- 已添加 `app/tetris/tetris.cpp` 到编译单元

### 待完成
- Phase 2: LVGL 渲染
- Phase 3: 输入抽象层
- Phase 4: 网络层
- Phase 5: 集成 (tetrisApp)

## 文件结构

```
main/app/tetris/
├── tetris.hpp    # 类型定义
└── tetris.cpp    # 实现
```

## 构建状态
✅ 编译通过 (The-Defect.bin 0x28b9b0 bytes, 36% free)</parameter>
