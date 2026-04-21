package models

import (
	"time"

	"gorm.io/gorm"
)

// Firmware 固件模型
type Firmware struct {
	ID          uint           `gorm:"primaryKey" json:"id"`
	Version     string         `gorm:"size:50;not null" json:"version"`
	Target      string         `gorm:"size:20;not null" json:"target"` // esp32, stm32
	FilePath    string         `gorm:"size:255;not null" json:"file_path"`
	Checksum    string         `gorm:"size:64;not null" json:"checksum"`
	Description string         `gorm:"type:text" json:"description"`
	IsCurrent   bool           `gorm:"default:false" json:"is_current"`
	CreatedAt   time.Time      `json:"created_at"`
	UpdatedAt   time.Time      `json:"updated_at"`
	DeletedAt   gorm.DeletedAt `gorm:"index" json:"-"`
}

// Device 设备模型
type Device struct {
	ID             string    `gorm:"primaryKey;size:64" json:"id"` // MAC 或 UUID
	Name           string    `gorm:"size:100" json:"name"`
	CurrentVersion string    `gorm:"size:50" json:"current_version"`
	LastSeen       time.Time `json:"last_seen"`
	IPAddress      string    `gorm:"size:45" json:"ip_address"`
}

// OTALog 升级日志
type OTALog struct {
	ID          uint      `gorm:"primaryKey" json:"id"`
	DeviceID    string    `gorm:"size:64;index" json:"device_id"`
	FromVersion string    `gorm:"size:50" json:"from_version"`
	ToVersion   string    `gorm:"size:50" json:"to_version"`
	Status      string    `gorm:"size:20" json:"status"` // pending, success, failed
	ErrorMsg    string    `gorm:"type:text" json:"error_msg"`
	CreatedAt   time.Time `json:"created_at"`
}
