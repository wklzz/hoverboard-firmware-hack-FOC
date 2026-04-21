package controllers

import (
	"crypto/sha256"
	"fmt"
	"hoverboard-admin/config"
	"hoverboard-admin/models"
	"io"
	"net/http"
	"os"
	"path/filepath"

	"github.com/gin-gonic/gin"
)

// UploadFirmware 处理固件上传
func UploadFirmware(c *gin.Context) {
	version := c.PostForm("version")
	target := c.PostForm("target")
	description := c.PostForm("description")

	file, err := c.FormFile("file")
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "No file uploaded"})
		return
	}

	// 创建目录
	uploadDir := "./uploads"
	if _, err := os.Stat(uploadDir); os.IsNotExist(err) {
		os.Mkdir(uploadDir, 0755)
	}

	// 保存文件
	filename := filepath.Base(file.Filename)
	savePath := filepath.Join(uploadDir, fmt.Sprintf("%s_%s", version, filename))
	if err := c.SaveUploadedFile(file, savePath); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to save file"})
		return
	}

	// 计算 SHA256
	f, _ := os.Open(savePath)
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to calculate checksum"})
		return
	}
	checksum := fmt.Sprintf("%x", h.Sum(nil))

	// 存入数据库
	firmware := models.Firmware{
		Version:     version,
		Target:      target,
		FilePath:    savePath,
		Checksum:    checksum,
		Description: description,
	}

	config.DB.Create(&firmware)

	c.JSON(http.StatusOK, gin.H{"message": "Uploaded successfully", "data": firmware})
}

// SetCurrentFirmware 设置当前发布版本
func SetCurrentFirmware(c *gin.Context) {
	id := c.Param("id")
	var firmware models.Firmware
	if err := config.DB.First(&firmware, id).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "Firmware not found"})
		return
	}

	// 将同 target 的其他固件设置为非当前版本
	config.DB.Model(&models.Firmware{}).Where("target = ?", firmware.Target).Update("is_current", false)

	// 设置当前版本
	config.DB.Model(&firmware).Update("is_current", true)

	c.JSON(http.StatusOK, gin.H{"message": "Set as current version"})
}
