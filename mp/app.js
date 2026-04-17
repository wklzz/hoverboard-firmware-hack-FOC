App({
  onLaunch() {
    // 展示本地存储能力
    const logs = wx.getStorageSync('logs') || []
    logs.unshift(Date.now())
    wx.setStorageSync('logs', logs)

    // 登录
    wx.login({
      success: res => {
        // 发送 res.code 到后台换取 openId, sessionKey, unionId
      }
    })
  },
  globalData: {
    userInfo: null,
    deviceId: '',
    serviceId: '6E400001-B5A3-F393-E0A9-E50E24DCCA9E',
    writeCharId: '6E400002-B5A3-F393-E0A9-E50E24DCCA9E',
    readCharId: '6E400003-B5A3-F393-E0A9-E50E24DCCA9E'
  }
})
