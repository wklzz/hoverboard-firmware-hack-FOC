<template>
  <div class="keys">
    <div class="header">
      <h2>开发者公钥管理</h2>
      <el-button type="primary" @click="dialogVisible = true">添加公钥</el-button>
    </div>

    <el-table :data="keys" style="width: 100%">
      <el-table-column prop="id" label="ID" width="60" />
      <el-table-column prop="name" label="名称" width="120" />
      <el-table-column prop="fingerprint" label="指纹" />
      <el-table-column label="最后使用" width="180">
        <template #default="scope">
          {{ scope.row.last_used_at ? new Date(scope.row.last_used_at).toLocaleString() : '从未使用' }}
        </template>
      </el-table-column>
      <el-table-column label="操作" width="100">
        <template #default="scope">
          <el-button size="small" type="danger" @click="handleDelete(scope.row.id)">删除</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog v-model="dialogVisible" title="添加公钥" width="500px">
      <el-form :model="form" label-width="80px">
        <el-form-item label="名称">
          <el-input v-model="form.name" placeholder="例如: Macbook-Home" />
        </el-form-item>
        <el-form-item label="公钥内容">
          <el-input 
            v-model="form.public_key" 
            type="textarea" 
            :rows="6" 
            placeholder="-----BEGIN PUBLIC KEY----- ..." 
          />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" @click="submitAdd">添加</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { getKeys, addKey, deleteKey } from '../api'
import { ElMessage } from 'element-plus'

const keys = ref([])
const dialogVisible = ref(false)
const form = ref({ name: '', public_key: '' })

const loadData = async () => {
  const res = await getKeys()
  keys.value = res.data || []
}

const submitAdd = async () => {
  if (!form.value.name || !form.value.public_key) return ElMessage.error('请填写完整信息')
  try {
    await addKey(form.value)
    ElMessage.success('添加成功')
    dialogVisible.value = false
    form.value = { name: '', public_key: '' }
    loadData()
  } catch (e) {
    ElMessage.error('添加失败，请检查格式')
  }
}

const handleDelete = async (id: number) => {
  try {
    await deleteKey(id)
    ElMessage.success('删除成功')
    loadData()
  } catch (e) {
    ElMessage.error('删除失败')
  }
}

onMounted(loadData)
</script>

<style scoped>
.keys { padding: 20px; }
.header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
</style>
