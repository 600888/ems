import type { DeviceNode } from './domain'

export type RoleCode = 'admin' | 'operator' | 'observer'
export type UserStatus = 'enabled' | 'disabled' | 'deleted'
export interface ResourceScope {
  station_id: string
  device_root_id: string | null
}
export interface IdentityUser {
  user_id: string
  username: string
  display_name: string
  role_code: RoleCode
}
export interface ManagedUser extends IdentityUser {
  status: UserStatus
  scope_mode: 'all' | 'selected'
  scopes: ResourceScope[]
  scope_labels?: string[]
  must_change_password: boolean
  locked_until: string | null
  created_at: string
  updated_at: string
  last_login_at: string | null
  password_changed_at: string | null
  edit_token: string
}
export interface LoginPolicy {
  failed_login_limit: number
  lock_minutes: number
  idle_timeout_minutes: number
  password_min_length: number
  login_notice: string
  edit_token?: string
}
export interface LoginOptions {
  login_notice: string
  password_min_length: number
  password_max_length: number
}
export interface AuthSession {
  kind: 'full' | 'password_change'
  next_action?: 'enter_app' | 'change_password'
  user: IdentityUser
  permissions?: string[]
  scopes?: ResourceScope[]
  scope_mode?: 'all' | 'selected'
  csrf_token: string
  idle_expires_at: string
  absolute_expires_at: string
  auth_time?: string
  password_min_length?: number
}
export interface SessionSummary {
  session_id: string
  client: string
  source_ip: string
  created_at: string
  last_interactive_at: string
  idle_expires_at: string
  absolute_expires_at: string
  current: boolean
}
export interface UserListQuery {
  keyword?: string
  role_code?: RoleCode | ''
  status?: UserStatus | ''
  page: number
  page_size: number
}
export interface PageResult<T> {
  items: T[]
  total: number
  page: number
  page_size: number
}
export interface UserListResult extends PageResult<ManagedUser> {
  stats: { total: number; enabled: number; disabled: number; locked: number }
}
export interface UserCapabilities extends LoginOptions {
  scope_tree: DeviceNode[]
}
export interface UserAccess {
  role_code: RoleCode
  scope_mode: 'all' | 'selected'
  scopes: ResourceScope[]
}
export interface CreateUser extends UserAccess {
  username: string
  display_name: string
  status: 'enabled' | 'disabled'
  temporary_password: string
}
