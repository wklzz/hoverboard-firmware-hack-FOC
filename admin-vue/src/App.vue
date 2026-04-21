<template>
  <el-container class="layout-container">
    <el-aside width="200px">
      <div class="logo">平衡车管理后台</div>
      <el-menu
        router
        :default-active="$route.path"
        class="el-menu-vertical"
      >
        <el-menu-item index="/">
          <el-icon><Odometer /></el-icon>
          <span>仪表盘</span>
        </el-menu-item>
        <el-menu-item index="/firmwares">
          <el-icon><Box /></el-icon>
          <span>固件管理</span>
        </el-menu-item>
        <el-menu-item index="/devices">
          <el-icon><Cpu /></el-icon>
          <span>设备监控</span>
        </el-menu-item>
      </el-menu>
    </el-aside>
    <el-container>
      <el-header>
        <div class="header-content">
          <span>管理员</span>
        </div>
      </el-header>
      <el-main>
        <router-view></router-view>
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup lang="ts">
import { Odometer, Box, Cpu } from '@element-plus/icons-vue'
</script>

<style>
body { margin: 0; font-family: 'Inter', sans-serif; }
.layout-container { height: 100vh; }
.el-aside { background-color: #304156; color: white; }
.logo { height: 60px; line-height: 60px; text-align: center; font-weight: bold; background-color: #2b2f3a; }
.el-menu-vertical { border-right: none; background-color: transparent; }
.el-menu-item { color: #bfcbd9; }
.el-menu-item.is-active { color: #409EFF; background-color: #263445 !important; }
.el-header { background-color: white; border-bottom: 1px solid #e6e6e6; display: flex; align-items: center; justify-content: flex-end; padding: 0 20px; }
</style>
