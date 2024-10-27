export default {
  // jusprintChats: (chatId) =>
  //   chatId ? `/ent/api/jusprint/chats/${chatId}/` : `/ent/api/jusprint/chats/`,
  jusprintChats: (chatId) =>
    chatId ? `http://44.204.115.116:5555/ent/api/jusprint/chats/${chatId}/` : `http://44.204.115.116:5555/ent/api/jusprint/chats/`,
  // jusprintChatMessages: () => `/ent/api/jusprint/chats/messages/`,
  jusprintChatMessages: () => `http://44.204.115.116:5555/ent/api/jusprint/chats/messages/`,
}