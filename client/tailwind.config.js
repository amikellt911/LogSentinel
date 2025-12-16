/** @type {import('tailwindcss').Config} */
export default {
  darkMode: 'class',
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        bg: {
          dark: '#1e1e1e',
          darker: '#121212',
          lighter: '#2d2d2d'
        },
        primary: '#409eff', // Element Plus default blue
        success: '#67c23a',
        warning: '#e6a23c',
        danger: '#f56c6c',
        info: '#909399',
      },
      fontFamily: {
        mono: ['"Fira Code"', 'monospace', 'ui-monospace', 'SFMono-Regular']
      }
    },
  },
  plugins: [],
}
