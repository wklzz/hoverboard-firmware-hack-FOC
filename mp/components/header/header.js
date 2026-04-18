Component({
  properties: {
    connected: {
      type: Boolean,
      value: false
    },
    mode: {
      type: Number,
      value: 0
    }
  },
  methods: {
    onToggle() {
      this.triggerEvent('toggle');
    },
    onDisconnect() {
      this.triggerEvent('disconnect');
    },
    onUpdate() {
      wx.navigateTo({ url: '/pages/ota/ota' });
    }
  }
})
