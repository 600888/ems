import { request } from './http'
import type {
  AuthSession,
  LoginOptions,
  LoginPolicy,
  ManagedUser,
  PageResult,
  SessionSummary,
  UserCapabilities,
  UserListQuery,
  UserListResult,
  CreateUser,
  UserAccess,
} from '@/types/identity'

export const identityApi = {
  options: () => request<LoginOptions>('/api/auth/login-options', undefined, { anonymous: true }),
  session: () => request<AuthSession>('/api/auth/session'),
  login: (username: string, password: string) =>
    request<AuthSession>('/api/auth/login', { username, password }, { anonymous: true }),
  completePassword: (old_password: string, new_password: string) =>
    request('/api/auth/password/complete', { old_password, new_password }),
  reauthenticate: (password: string) => request('/api/auth/reauthenticate', { password }),
  activity: () => request<AuthSession>('/api/auth/activity', {}),
  logout: () => request('/api/auth/logout', {}),
  logoutAll: () => request('/api/auth/logout-all', {}),
  profile: () => request<ManagedUser>('/api/profile/me'),
  updateProfile: (display_name: string) =>
    request<ManagedUser>('/api/profile/update', { display_name }),
  changePassword: (old_password: string, new_password: string) =>
    request('/api/profile/change-password', { old_password, new_password }),
  sessions: () =>
    request<PageResult<SessionSummary>>('/api/profile/sessions/list', { page: 1, page_size: 20 }),
  revokeSession: (session_id: string) => request('/api/profile/sessions/revoke', { session_id }),
}

export const usersApi = {
  capabilities: () => request<UserCapabilities>('/api/users/capabilities'),
  list: (query: UserListQuery, signal?: AbortSignal) =>
    request<UserListResult>('/api/users/list', query, { signal }),
  detail: (user_id: string) => request<ManagedUser>('/api/users/detail', { user_id }),
  create: (user: CreateUser) => request<ManagedUser>('/api/users/create', user),
  update: (user_id: string, display_name: string, expected_edit_token: string) =>
    request<ManagedUser>('/api/users/update', { user_id, display_name, expected_edit_token }),
  access: (user_id: string, access: UserAccess, expected_edit_token: string) =>
    request<ManagedUser>('/api/users/set-access', { user_id, ...access, expected_edit_token }),
  action: (
    action: 'set-status' | 'unlock' | 'delete' | 'reset-password',
    user_id: string,
    expected_edit_token: string,
    extra = {},
  ) => request<ManagedUser>(`/api/users/${action}`, { user_id, expected_edit_token, ...extra }),
  sessions: (user_id: string) =>
    request<PageResult<SessionSummary>>('/api/users/sessions/list', {
      user_id,
      page: 1,
      page_size: 20,
    }),
  revokeSession: (user_id: string, session_id?: string) =>
    request('/api/users/sessions/revoke', {
      user_id,
      ...(session_id ? { session_id } : { all: true }),
    }),
  policy: () => request<LoginPolicy>('/api/users/security-settings'),
  savePolicy: (policy: LoginPolicy) => {
    const { edit_token, ...values } = policy
    return request<LoginPolicy>('/api/users/security-settings/update', {
      ...values,
      expected_edit_token: edit_token,
    })
  },
}
