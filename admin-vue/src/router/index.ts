import { createRouter, createWebHistory } from 'vue-router'
import Dashboard from '../views/Dashboard.vue'
import Firmwares from '../views/Firmwares.vue'
import Devices from '../views/Devices.vue'

const routes = [
  { path: '/', component: Dashboard },
  { path: '/firmwares', component: Firmwares },
  { path: '/devices', component: Devices }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

export default router
