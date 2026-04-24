package controllers

import (
	"crypto"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/base64"
	"encoding/pem"
	"fmt"
	"hoverboard-admin/config"
	"hoverboard-admin/models"
	"io"
	"net/http"
	"os"
	"path/filepath"
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

// CheckAllUpdates 检查所有目标的最新版本
func CheckAllUpdates(c *gin.Context) {
	var firmwares []models.Firmware
	// 查找所有 target 的 is_current = true 的固件
	if err := config.DB.Where("is_current = ?", true).Find(&firmwares).Error; err != nil {
		c.JSON(http.StatusOK, gin.H{"update": false, "message": "No stable version found"})
		return
	}

	results := []gin.H{}
	for _, fw := range firmwares {
		results = append(results, gin.H{
			"id":          fw.ID,
			"version":     fw.Version,
			"target":      fw.Target,
			"checksum":    fw.Checksum,
			"description": fw.Description,
			"url":         "/api/ota/download/" + fmt.Sprintf("%d", fw.ID),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"update":    len(results) > 0,
		"firmwares": results,
	})
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

// UploadSecure 自动推送接口 (带签名验证)
func UploadSecure(c *gin.Context) {
	keyID := c.GetHeader("X-Key-ID")
	signatureB64 := c.GetHeader("X-Signature")

	if keyID == "" || signatureB64 == "" {
		c.JSON(http.StatusUnauthorized, gin.H{"error": "Missing signature headers"})
		return
	}

	// 获取公钥
	var devKey models.DeveloperKey
	if err := config.DB.First(&devKey, keyID).Error; err != nil {
		c.JSON(http.StatusUnauthorized, gin.H{"error": "Invalid Key ID"})
		return
	}

	// 解析参数
	version := c.PostForm("version")
	target := c.PostForm("target")
	description := c.PostForm("description")
	file, err := c.FormFile("file")
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "File is required"})
		return
	}

	// 保存临时文件计算哈希
	tempPath := filepath.Join("uploads", "temp_"+file.Filename)
	if err := c.SaveUploadedFile(file, tempPath); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to save temp file"})
		return
	}
	defer os.Remove(tempPath)

	fileData, _ := os.ReadFile(tempPath)
	fileHash := sha256.Sum256(fileData)

	// 验证签名
	// 待签名数据：version + "|" + target + "|" + description + "|" + hex(fileHash)
	msg := fmt.Sprintf("%s|%s|%s|%x", version, target, description, fileHash)
	if err := verifySignature(devKey.PublicKey, msg, signatureB64); err != nil {
		c.JSON(http.StatusUnauthorized, gin.H{"error": "Signature verification failed: " + err.Error()})
		return
	}

	// 验证通过，正式保存
	finalName := fmt.Sprintf("%s_%s_%d.bin", target, version, time.Now().Unix())
	finalPath := filepath.Join("uploads", finalName)
	
	// 复制临时文件到最终路径 (因为 defer 删除了 tempPath)
	if err := copyFile(tempPath, finalPath); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to save firmware"})
		return
	}

	// 计算校验和
	checksum := fmt.Sprintf("%x", fileHash)

	// 存入数据库
	newFirmware := models.Firmware{
		Version:     version,
		Target:      target,
		FilePath:    finalPath,
		Checksum:    checksum,
		Description: description,
		IsCurrent:   false, // 自动推送的默认不发布，需手动在后台确认
	}

	if err := config.DB.Create(&newFirmware).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Database error"})
		return
	}

	// 更新 Key 的最后使用时间
	config.DB.Model(&devKey).Update("last_used_at", time.Now())

	c.JSON(http.StatusOK, gin.H{
		"message": "Upload successful",
		"id":      newFirmware.ID,
		"version": newFirmware.Version,
	})
}

func verifySignature(pubKeyPEM, message, signatureB64 string) error {
	block, _ := pem.Decode([]byte(pubKeyPEM))
	if block == nil {
		return fmt.Errorf("failed to parse PEM block")
	}

	pub, err := x509.ParsePKIXPublicKey(block.Bytes)
	if err != nil {
		return err
	}

	rsaPub, ok := pub.(*rsa.PublicKey)
	if !ok {
		return fmt.Errorf("not an RSA public key")
	}

	sig, err := base64.StdEncoding.DecodeString(signatureB64)
	if err != nil {
		return err
	}

	hash := sha256.Sum256([]byte(message))
	return rsa.VerifyPKCS1v15(rsaPub, crypto.SHA256, hash[:], sig)
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()
	_, err = io.Copy(out, in)
	return err
}
