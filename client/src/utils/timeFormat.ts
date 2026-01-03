/**
 * 时间格式化工具
 * 将后端返回的 UTC 时间转换为北京时间 (Asia/Shanghai, UTC+8)
 */

/**
 * 将 UTC 时间字符串转换为北京时间字符串
 * @param utcString - UTC 时间字符串，格式如 "2025-12-25 03:25:48"
 * @returns 北京时间字符串，格式如 "2025-12-25 11:25:48"
 */
export function formatToBeijingTime(utcString: string): string {
  if (!utcString) return ''

  try {
    // 解析 UTC 时间字符串（SQLite CURRENT_TIMESTAMP 返回的是 UTC 时间）
    const date = new Date(utcString + 'Z') // 添加 'Z' 表示这是 UTC 时间

    // 检查日期是否有效
    if (isNaN(date.getTime())) {
      // 如果解析失败，可能是已经包含时区信息的时间，直接返回
      return utcString
    }

    // 格式化为北京时间字符串
    const year = date.getFullYear()
    const month = String(date.getMonth() + 1).padStart(2, '0')
    const day = String(date.getDate()).padStart(2, '0')
    const hours = String(date.getHours()).padStart(2, '0')
    const minutes = String(date.getMinutes()).padStart(2, '0')
    const seconds = String(date.getSeconds()).padStart(2, '0')

    return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`
  } catch (error) {
    console.error('时间格式化错误:', error)
    return utcString
  }
}

/**
 * 将 UTC 时间字符串转换为相对时间描述
 * @param utcString - UTC 时间字符串
 * @returns 相对时间描述，如 "5分钟前"
 */
export function formatToRelativeTime(utcString: string): string {
  if (!utcString) return ''

  try {
    const date = new Date(utcString + 'Z')
    const now = new Date()
    const diffMs = now.getTime() - date.getTime()
    const diffSeconds = Math.floor(diffMs / 1000)
    const diffMinutes = Math.floor(diffSeconds / 60)
    const diffHours = Math.floor(diffMinutes / 60)
    const diffDays = Math.floor(diffHours / 24)

    if (diffSeconds < 60) {
      return '刚刚'
    } else if (diffMinutes < 60) {
      return `${diffMinutes}分钟前`
    } else if (diffHours < 24) {
      return `${diffHours}小时前`
    } else if (diffDays < 7) {
      return `${diffDays}天前`
    } else {
      // 超过7天显示完整时间
      return formatToBeijingTime(utcString)
    }
  } catch (error) {
    console.error('相对时间格式化错误:', error)
    return utcString
  }
}
