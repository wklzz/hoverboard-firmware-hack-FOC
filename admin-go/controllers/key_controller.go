package controllers

import (
	"crypto/sha256"
	"fmt"
	"hoverboard-admin/config"
	"hoverboard-admin/models"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
)

// ListKeys 获取公钥列表
func ListKeys(c *gin.Context) {
	var keys []models.DeveloperKey
	config.DB.Find(&keys)
	c.JSON(http.StatusOK, keys)
}

// AddKey 添加公钥
func AddKey(c *gin.Context) {
	var input struct {
		Name      string `json:"name" binding:"required"`
		PublicKey string `json:"public_key" binding:"required"`
	}

	if err := c.ShouldBindJSON(&input); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	// 计算指纹 (SHA256)
	fingerprint := fmt.Sprintf("%x", sha256.Sum256([]byte(input.PublicKey)))

	key := models.DeveloperKey{
		Name:        input.Name,
		PublicKey:   input.PublicKey,
		Fingerprint: fingerprint,
		CreatedAt:   time.Now(),
	}

	if err := config.DB.Create(&key).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to save key"})
		return
	}

	c.JSON(http.StatusOK, key)
}

// DeleteKey 删除公钥
func DeleteKey(c *gin.Context) {
	id := c.Param("id")
	if err := config.DB.Delete(&models.DeveloperKey{}, id).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to delete key"})
		return
	}
	c.JSON(http.StatusOK, gin.H{"message": "Key deleted"})
}
