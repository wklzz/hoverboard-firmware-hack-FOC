<template>
  <div class="devices">
    <h2>设备监控</h2>
    <el-table :data="devices" style="width: 100%">
      <el-table-column prop="id" label="设备ID" width="200" />
      <el-table-column prop="name" label="名称" width="150" />
      <el-table-column prop="current_version" label="当前版本" width="120" />
      <el-table-column prop="ip_address" label="最后在线IP" width="150" />
      <el-table-column label="最后活跃" width="180">
        <template #default="scope">
          {{ new Date(scope.row.last_seen).toLocaleString() }}
        </template>
      </el-table-column>
    </el-table>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { getDevices } from '../api'

const devices = ref([])

onMounted(async () => {
  const res = await getDevices()
  devices.value = res.data || []
})
</script>

<style scoped>
.devices { padding: 20px; }
</style>
