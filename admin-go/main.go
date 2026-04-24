package main

import (
	"hoverboard-admin/config"
	"hoverboard-admin/controllers"
	"hoverboard-admin/models"
	"net/http"

	"github.com/gin-gonic/gin"
)

func main() {
	// 1. 初始化数据库
	config.InitDB()

	// 2. 自动迁移模型
	config.DB.AutoMigrate(&models.Firmware{}, &models.Device{}, &models.OTALog{}, &models.DeveloperKey{})

	// 3. 设置 Gin 路由
	r := gin.Default()

	// 跨域支持 (如果需要)
	r.Use(func(c *gin.Context) {
		c.Writer.Header().Set("Access-Control-Allow-Origin", "*")
		c.Writer.Header().Set("Access-Control-Allow-Methods", "POST, GET, OPTIONS, PUT, DELETE")
		c.Writer.Header().Set("Access-Control-Allow-Headers", "Content-Type, Content-Length, Accept-Encoding, X-CSRF-Token, Authorization, X-Key-ID, X-Signature")
		if c.Request.Method == "OPTIONS" {
			c.AbortWithStatus(204)
			return
		}
		c.Next()
	})

	// 基础测试接口
	r.GET("/ping", func(c *gin.Context) {
		c.JSON(http.StatusOK, gin.H{
			"message": "pong",
		})
	})

	// 固件管理
	r.GET("/api/firmwares", func(c *gin.Context) {
		var firmwares []models.Firmware
		config.DB.Order("created_at desc").Find(&firmwares)
		c.JSON(http.StatusOK, firmwares)
	})
	r.POST("/api/firmwares/upload", controllers.UploadFirmware)
	r.POST("/api/firmwares/set-current/:id", controllers.SetCurrentFirmware)

	// 公钥管理
	r.GET("/api/keys", controllers.ListKeys)
	r.POST("/api/keys", controllers.AddKey)
	r.DELETE("/api/keys/:id", controllers.DeleteKey)

	// 设备管理
	r.GET("/api/devices", func(c *gin.Context) {
		var devices []models.Device
		config.DB.Order("last_seen desc").Find(&devices)
		c.JSON(http.StatusOK, devices)
	})

	// OTA 接口 (供硬件及脚本调用)
	r.GET("/api/ota/check", controllers.CheckUpdate)
	r.GET("/api/ota/check-all", controllers.CheckAllUpdates)
	r.GET("/api/ota/download/:id", controllers.DownloadFirmware)
	r.POST("/api/ota/upload-secure", controllers.UploadSecure)

	// 4. 启动服务
	r.Run(":8080")
}
