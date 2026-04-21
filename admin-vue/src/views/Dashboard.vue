<template>
  <div class="dashboard">
    <el-row :gutter="20">
      <el-col :span="8">
        <el-card shadow="hover">
          <template #header>设备总数</template>
          <div class="card-content">{{ deviceCount }}</div>
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card shadow="hover">
          <template #header>固件版本数</template>
          <div class="card-content">{{ firmwareCount }}</div>
        </el-card>
      </el-col>
      <el-col :span="8">
        <el-card shadow="hover">
          <template #header>系统状态</template>
          <div class="card-content text-success">运行中</div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { getDevices, getFirmwares } from '../api'

const deviceCount = ref(0)
const firmwareCount = ref(0)

onMounted(async () => {
  const devicesRes = await getDevices()
  const firmwaresRes = await getFirmwares()
  deviceCount.value = (devicesRes.data || []).length
  firmwareCount.value = (firmwaresRes.data || []).length
})
</script>

<style scoped>
.dashboard {
  padding: 20px;
}
.card-content {
  font-size: 32px;
  font-weight: bold;
  text-align: center;
}
.text-success {
  color: #67C23A;
}
</style>
