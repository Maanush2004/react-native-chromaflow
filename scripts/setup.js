// scripts/setup.js
const { execSync } = require('child_process')
const path = require('path')
const fs = require('fs')

const root = path.join(__dirname, '..')

const deps = [
  { path: 'cpp/jabcode', url: 'https://github.com/jabcode/jabcode.git', ref: '76a7655bb61e65f81ea962e575cdbd06fedebb26' },
  { path: 'cpp/libpng',  url: 'https://github.com/pnggroup/libpng.git', ref: 'v1.6.56' },
  { path: 'cpp/libtiff', url: 'https://gitlab.com/libtiff/libtiff.git', ref: 'v4.7.1'  },
]

for (const dep of deps) {
  const full = path.join(root, dep.path)
  if (!fs.existsSync(full) || fs.readdirSync(full).length === 0) {
    console.log(`[chromaflow] Cloning ${dep.url}@${dep.ref}...`)
    execSync(`git clone ${dep.url} ${full}`, { stdio: 'inherit' })
    execSync(`git -C ${full} checkout ${dep.ref}`, { stdio: 'inherit' })
  } else {
    console.log(`[chromaflow] ${dep.path} already present, skipping.`)
  }
}