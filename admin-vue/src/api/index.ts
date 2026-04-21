import axios from 'axios'

const api = axios.create({
  baseURL: '/api', // 指向反向代理转发后的 API 路径
  timeout: 5000
})

export const getFirmwares = () => api.get('/firmwares')
export const uploadFirmware = (formData: FormData) => api.post('/firmwares/upload', formData)
export const setCurrentFirmware = (id: number) => api.post(`/firmwares/set-current/${id}`)
export const getDevices = () => api.get('/devices')

export default api
