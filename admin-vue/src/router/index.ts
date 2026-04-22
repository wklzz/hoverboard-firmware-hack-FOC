import { createRouter, createWebHistory } from 'vue-router'
import Dashboard from '../views/Dashboard.vue'
import Firmwares from '../views/Firmwares.vue'
import Devices from '../views/Devices.vue'
import Keys from '../views/Keys.vue'

const routes = [
  { path: '/', component: Dashboard },
  { path: '/firmwares', component: Firmwares },
  { path: '/devices', component: Devices },
  { path: '/keys', component: Keys }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

export default router
