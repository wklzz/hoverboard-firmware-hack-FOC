import { bleManager } from './ble';
import { compareVersions } from './protocol';

const BASE_URL = 'http://wcart.wozer.cn/api';

/**
 * OTA Helper for Mini Program
 */
export const otaHelper = {
  /**
   * Check for updates on all targets and prompt user if found
   * @param {Object} currentVersions - {esp: 'v1.x', stm: 'v1.x'}
   * @param {boolean} isManual - If true, show a toast when no update is found
   */
  async checkAndPrompt(currentVersions, isManual = false) {
    console.log(`[OTA Helper] Starting checkAll (Manual: ${isManual})...`);
    if (!bleManager.connected) {
      if (isManual) wx.showToast({ title: '蓝牙未连接', icon: 'none' });
      return;
    }

    if (isManual) wx.showLoading({ title: '正在检查更新...', mask: true });

    return new Promise((resolve) => {
      wx.request({
        url: `${BASE_URL}/ota/check-all`,
        success: (res) => {
          if (isManual) wx.hideLoading();
          
          console.log('[OTA Helper] Check result:', res.data);
          if (res.data && res.data.update) {
            const updates = res.data.firmwares;
            const pending = [];

            updates.forEach(fw => {
              const local = (fw.target === 'esp32') ? currentVersions.esp : currentVersions.stm;
              console.log(`[OTA Helper] Comparing ${fw.target}: Cloud=${fw.version}, Local=${local}`);
              
              // 逻辑：只有当 云端版本 > 本地版本 时才升级；如果本地未知，也升级
              let shouldUpgrade = false;
              if (!local || local === '未知' || local.includes('unknown')) {
                shouldUpgrade = true;
              } else if (compareVersions(fw.version, local) > 0) {
                shouldUpgrade = true;
              }

              if (shouldUpgrade) {
                pending.push(fw);
              }
            });

            // 优先级排序：ESP32 优先升级
            pending.sort((a, b) => {
              if (a.target === 'esp32' && b.target !== 'esp32') return -1;
              if (a.target !== 'esp32' && b.target === 'esp32') return 1;
              return 0;
            });

            console.log('[OTA Helper] Pending tasks (Sorted):', pending);
            if (pending.length > 0) {
              this.showUpdateModal(pending);
            } else if (isManual) {
              wx.showToast({ title: '已是最新版本', icon: 'success' });
            }
          } else if (isManual) {
            wx.showToast({ title: '已是最新版本', icon: 'success' });
          }
          resolve();
        },
        fail: (err) => {
          if (isManual) {
            wx.hideLoading();
            wx.showToast({ title: '检查失败', icon: 'none' });
          }
          console.error('[OTA Helper] Network error:', err);
          resolve();
        }
      });
    });
  },

  showUpdateModal(pending) {
    const titles = pending.map(f => `${f.target.toUpperCase()} v${f.version}`).join(', ');
    wx.showModal({
      title: '发现新固件',
      content: `检测到以下更新：\n${titles}\n\n建议立即升级以获得最佳性能。是否进入升级页面？`,
      confirmText: '立即升级',
      cancelText: '稍后再说',
      success: (res) => {
        if (res.confirm) {
          console.log('[OTA Helper] User confirmed upgrade. Queueing:', pending);
          const app = getApp();
          app.globalData.otaQueue = pending;
          console.log('[OTA Helper] globalData.otaQueue set. Navigating to OTA page...');
          
          wx.navigateTo({
            url: '/pages/ota/ota?auto=1',
            success: () => console.log('[OTA Helper] Navigation success'),
            fail: (e) => console.error('[OTA Helper] Navigation failed:', e)
          });
        }
      }
    });
  }
};
