import React from 'react';

export default function PCControllerImprovementReport() {
  const phases = [
    {
      id: 1,
      name: '显示层修复',
      commit: '8ee2ec0 + 09db37a',
      status: '✅ 完成',
      items: [
        'swap_bytes Kconfig 化 (CONFIG_BSP_LCD_SWAP_BYTES)',
        'LVGL 内存池 24KB → 32KB',
        'rotation 实测历史文档化',
        'lv_obj_create() NULL 检查补全'
      ]
    },
    {
      id: 2,
      name: 'BLE/HID 稳定性',
      commit: 'e02cce5',
      status: '✅ 完成',
      items: [
        '槽位地址类型扩展 (pc_slot_t.addr_type)',
        '槽位清除联动 NimBLE 绑定删除',
        '键盘 release 失败 50ms 重试',
        '跨平台三连发快捷键 (F5/Cmd+Shift+Enter/Opt+Cmd+P)',
        '两步式 BLE 重置 (菜单项 8, 3 秒武装窗口)',
        'NimBLE host 复位状态完整清理'
      ]
    },
    {
      id: 3,
      name: '架构简化与文档',
      commit: 'ff610a0',
      status: '✅ 完成',
      items: [
        '删除废弃 sdkconfig.presenter.defaults',
        'CMake/Kconfig 注释与实现同步',
        '新增 display-pitfalls.md + ble-pitfalls.md (中英双语)',
        '转场顺序注释修正',
        '音量上限统一为 99'
      ]
    },
    {
      id: 4,
      name: '功能增强',
      commit: '106130c',
      status: '✅ 完成',
      items: [
        '串口截屏协议 (pc_screenshot.c/h, FAP_SCREENSHOT_V1)',
        '演讲计时器暂停/恢复 (OK 长按切换)',
        'STANDBY 未连接显示 DISCONNECTED 稳态',
        '广播数据添加 0x1812 HID service UUID'
      ]
    }
  ];

  const stats = {
    files: 37,
    additions: 1304,
    deletions: 167,
    commits: 5,
    checkRepo: 'PASS (224 files)',
    tests: 'PASS (2 tests)'
  };

  return (
    <div style={{ fontFamily: 'system-ui, sans-serif', padding: '20px', maxWidth: '900px', margin: '0 auto' }}>
      <h1 style={{ fontSize: '24px', marginBottom: '20px', borderBottom: '2px solid #333', paddingBottom: '10px' }}>
        PC Controller 彻底改进计划 - 完成报告
      </h1>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '16px', marginBottom: '30px' }}>
        <div style={{ background: '#f5f5f5', padding: '16px', borderRadius: '8px' }}>
          <div style={{ fontSize: '32px', fontWeight: 'bold', color: '#2563eb' }}>{stats.files}</div>
          <div style={{ fontSize: '14px', color: '#666' }}>文件修改</div>
        </div>
        <div style={{ background: '#f5f5f5', padding: '16px', borderRadius: '8px' }}>
          <div style={{ fontSize: '32px', fontWeight: 'bold', color: '#16a34a' }}>+{stats.additions}</div>
          <div style={{ fontSize: '14px', color: '#666' }}>代码新增</div>
        </div>
        <div style={{ background: '#f5f5f5', padding: '16px', borderRadius: '8px' }}>
          <div style={{ fontSize: '32px', fontWeight: 'bold', color: '#dc2626' }}>-{stats.deletions}</div>
          <div style={{ fontSize: '14px', color: '#666' }}>代码删除</div>
        </div>
      </div>

      <h2 style={{ fontSize: '18px', marginBottom: '16px' }}>阶段完成状态</h2>
      
      {phases.map(phase => (
        <div key={phase.id} style={{ marginBottom: '20px', border: '1px solid #e5e7eb', borderRadius: '8px', overflow: 'hidden' }}>
          <div style={{ background: '#f9fafb', padding: '12px 16px', borderBottom: '1px solid #e5e7eb', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <div>
              <span style={{ fontWeight: 'bold', marginRight: '12px' }}>阶段 {phase.id}: {phase.name}</span>
              <span style={{ fontSize: '12px', color: '#666', fontFamily: 'monospace' }}>{phase.commit}</span>
            </div>
            <span style={{ color: '#16a34a', fontWeight: 'bold' }}>{phase.status}</span>
          </div>
          <ul style={{ margin: 0, padding: '12px 16px 12px 32px', fontSize: '14px' }}>
            {phase.items.map((item, idx) => (
              <li key={idx} style={{ marginBottom: '6px' }}>{item}</li>
            ))}
          </ul>
        </div>
      ))}

      <h2 style={{ fontSize: '18px', marginBottom: '16px', marginTop: '30px' }}>验证结果</h2>
      <div style={{ background: '#f0fdf4', border: '1px solid #86efac', borderRadius: '8px', padding: '16px', marginBottom: '20px' }}>
        <div style={{ marginBottom: '8px' }}><strong>check_repo.py:</strong> {stats.checkRepo}</div>
        <div><strong>test_verify_firmware.py:</strong> {stats.tests}</div>
      </div>

      <h2 style={{ fontSize: '18px', marginBottom: '16px' }}>待用户真机验证</h2>
      <div style={{ background: '#fef3c7', border: '1px solid #fcd34d', borderRadius: '8px', padding: '16px' }}>
        <ol style={{ margin: 0, paddingLeft: '20px', fontSize: '14px' }}>
          <li style={{ marginBottom: '6px' }}>显示颜色 (swap_bytes=false 批次适配)</li>
          <li style={{ marginBottom: '6px' }}>跨平台快捷键 (Windows/macOS 全屏)</li>
          <li style={{ marginBottom: '6px' }}>两步式 BLE 重置流程</li>
          <li style={{ marginBottom: '6px' }}>串口截屏输出</li>
          <li>计时器暂停/恢复</li>
        </ol>
      </div>

      <div style={{ marginTop: '30px', paddingTop: '16px', borderTop: '1px solid #e5e7eb', fontSize: '12px', color: '#666' }}>
        生成时间：2026-09-02 | 分支：feature/pc-controller | 最新 commit: 106130c
      </div>
    </div>
  );
}
