<template>
  <div class="firmwares">
    <div class="header">
      <h2>固件管理</h2>
      <el-button type="primary" @click="dialogVisible = true">上传固件</el-button>
    </div>

    <el-table :data="firmwares" style="width: 100%">
      <el-table-column prop="version" label="版本" width="120" />
      <el-table-column prop="target" label="硬件目标" width="120" />
      <el-table-column prop="description" label="说明" />
      <el-table-column label="状态" width="150">
        <template #default="scope">
          <el-tag :type="scope.row.is_current ? 'success' : 'info'">
            {{ scope.row.is_current ? '已发布' : '历史版本' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="操作" width="180">
        <template #default="scope">
          <el-button 
            size="small" 
            type="primary" 
            :disabled="scope.row.is_current"
            @click="handleSetCurrent(scope.row.id)"
          >发布</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog v-model="dialogVisible" title="上传固件" width="400px">
      <el-form :model="form" label-width="80px">
        <el-form-item label="版本">
          <el-input v-model="form.version" placeholder="1.0.0" />
        </el-form-item>
        <el-form-item label="硬件">
          <el-select v-model="form.target" placeholder="请选择">
            <el-option label="ESP32" value="esp32" />
            <el-option label="STM32" value="stm32" />
          </el-select>
        </el-form-item>
        <el-form-item label="说明">
          <el-input v-model="form.description" type="textarea" />
        </el-form-item>
        <el-form-item label="文件">
          <input type="file" @change="handleFileChange" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" @click="submitUpload">提交</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { getFirmwares, uploadFirmware, setCurrentFirmware } from '../api'
import { ElMessage } from 'element-plus'

const firmwares = ref([])
const dialogVisible = ref(false)
const form = ref({ version: '', target: 'esp32', description: '' })
const selectedFile = ref<File | null>(null)

const loadData = async () => {
  const res = await getFirmwares()
  firmwares.value = res.data || []
}

const handleFileChange = (e: any) => {
  selectedFile.value = e.target.files[0]
}

const submitUpload = async () => {
  if (!selectedFile.value) return ElMessage.error('请选择文件')
  const formData = new FormData()
  formData.append('version', form.value.version)
  formData.append('target', form.value.target)
  formData.append('description', form.value.description)
  formData.append('file', selectedFile.value)

  try {
    await uploadFirmware(formData)
    ElMessage.success('上传成功')
    dialogVisible.value = false
    loadData()
  } catch (e) {
    ElMessage.error('上传失败')
  }
}

const handleSetCurrent = async (id: number) => {
  await setCurrentFirmware(id)
  ElMessage.success('发布成功')
  loadData()
}

onMounted(loadData)
</script>

<style scoped>
.firmwares { padding: 20px; }
.header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
</style>
