import axios from 'axios'

const api = axios.create({
  baseURL: '/api', // 指向反向代理转发后的 API 路径
  timeout: 5000
})

export const getFirmwares = () => api.get('/firmwares')
export const uploadFirmware = (formData: FormData) => api.post('/firmwares/upload', formData)
export const setCurrentFirmware = (id: number) => api.post(`/firmwares/set-current/${id}`)
export const getDevices = () => api.get('/devices')

export const getKeys = () => api.get('/keys')
export const addKey = (data: { name: string, public_key: string }) => api.post('/keys', data)
export const deleteKey = (id: number) => api.delete(`/keys/${id}`)

export default api
