package controllers

import (
	"fmt"
	"hoverboard-admin/config"
	"hoverboard-admin/models"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
)

// compareVersions 比较版本号 (v1 > v2 返回 1, v1 < v2 返回 -1, 相等返回 0)
func compareVersions(v1, v2 string) int {
	clean := func(s string) string {
		for i, r := range s {
			if r >= '0' && r <= '9' {
				return s[i:]
			}
		}
		return s
	}
	s1, s2 := clean(v1), clean(v2)
	var p1, p2 int
	for {
		if s1 == "" && s2 == "" {
			return 0
		}
		v1_part, v2_part := 0, 0
		fmt.Sscanf(s1, "%d", &v1_part)
		fmt.Sscanf(s2, "%d", &v2_part)

		if v1_part > v2_part {
			return 1
		}
		if v1_part < v2_part {
			return -1
		}

		// Move to next part
		if i := fmt.Sprint(v1_part); len(s1) > len(i) && s1[len(i)] == '.' {
			s1 = s1[len(i)+1:]
		} else {
			s1 = ""
		}
		if i := fmt.Sprint(v2_part); len(s2) > len(i) && s2[len(i)] == '.' {
			s2 = s2[len(i)+1:]
		} else {
			s2 = ""
		}
	}
}

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

	// 比较版本：只有当云端版本 > 当前版本时才提示更新
	if compareVersions(latestFirmware.Version, currentVersion) > 0 {
		c.JSON(http.StatusOK, gin.H{
			"update":      true,
			"id":          latestFirmware.ID,
			"version":     latestFirmware.Version,
			"checksum":    latestFirmware.Checksum,
			"description": latestFirmware.Description,
			"url":         "/api/ota/download/" + fmt.Sprintf("%d", latestFirmware.ID),
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
