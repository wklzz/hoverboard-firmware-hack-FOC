package controllers

import (
	"fmt"
	"hoverboard-admin/config"
	"hoverboard-admin/models"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
)

// CheckUpdate 硬件请求检查更新
func CheckUpdate(c *gin.Context) {
	deviceID := c.Query("device_id")
	currentVersion := c.Query("version")
	target := c.Query("target")

	// 更新或记录设备状态
	var device models.Device
	if err := config.DB.First(&device, "id = ?", deviceID).Error; err != nil {
		device = models.Device{
			ID:             deviceID,
			CurrentVersion: currentVersion,
			LastSeen:       time.Now(),
			IPAddress:      c.ClientIP(),
		}
		config.DB.Create(&device)
	} else {
		config.DB.Model(&device).Updates(models.Device{
			CurrentVersion: currentVersion,
			LastSeen:       time.Now(),
			IPAddress:      c.ClientIP(),
		})
	}

	// 查找最新发布的固件
	var latestFirmware models.Firmware
	if err := config.DB.Where("target = ? AND is_current = ?", target, true).First(&latestFirmware).Error; err != nil {
		c.JSON(http.StatusOK, gin.H{"update": false, "message": "No stable version found"})
		return
	}

	// 比较版本
	if latestFirmware.Version != currentVersion {
		c.JSON(http.StatusOK, gin.H{
			"update":   true,
			"version":  latestFirmware.Version,
			"checksum": latestFirmware.Checksum,
			"url":      "/api/ota/download/" + fmt.Sprintf("%d", latestFirmware.ID),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{"update": false})
}

// DownloadFirmware 下载固件文件
func DownloadFirmware(c *gin.Context) {
	id := c.Param("id")
	var firmware models.Firmware
	if err := config.DB.First(&firmware, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "File not found"})
		return
	}

	c.File(firmware.FilePath)
}
