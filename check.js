require('dotenv').config();
const uri = process.env.MONGO_URI || '(MONGO_URI is empty)';
console.log(uri.replace(/:\/\/([^:]+):[^@]+@/, '://$1:****@'));