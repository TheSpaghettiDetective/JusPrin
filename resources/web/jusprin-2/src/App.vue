<script setup>
import { ref, watchEffect, onMounted } from 'vue'
import axios from 'axios'
import markdownit from 'markdown-it'
import QuickActionButton from './components/QuickActionButton.vue'
import urls from './api'

const md = markdownit()
const emit = defineEmits(['change-presets', 'slice-model'])

// Data
const userInput = ref('')
const thinking = ref(false)
const jusprintCurrentChatId = ref('')
const messages = ref([
  {
    role: 'assistant',
    content:
      "To improve bed adhesion and ensure your print sticks better to the bed, consider the following adjustments: 1. **Increase the Brim Width:** Increasing the brim width from 5mm to 10mm can help improve adhesion by providing more surface area contact with the bed. 2. **Increase Initial Layer Line Width:** Consider setting the initial layer line width to 120% (0.48mm) to ensure better first-layer adhesion. 3. **Adjust Initial Layer Speed:** Lowering the initial layer speed from 15mm/s to 10mm/s gives the filament more time to adhere properly to the bed surface. 4. **Bed Temperature:** Ensure that the bed temperature is appropriate for the filament you're using. Since you are using PLA, the temperature settings provided (60°C) are suitable, but it's always good to ensure the bed surface is clean and prepared, like using an adhesive spray or painter's tape specifically for PLA. Following these steps should help your print adhere better to the print bed.",
    params: {
      filament_json: {},
      print_process_json: {
        brim_width: '10',
        initial_layer_line_width: '0.48',
        initial_layer_speed: '10',
      },
    },
  },
])
const quickActions = ref([])

// Computed properties
// const { selectedMachine, selectedFilament, selectedPrintProcess, jusprintCurrentChatId } = store.state.sliceSettingsStore

// const originalFilamentProfileName = computed(() => selectedFilament?.name || '')
// const originalPrintProcessProfileName = computed(() => selectedPrintProcess?.name || '')
// const filamentOverrideProfileName = computed(() => `${originalFilamentProfileName.value} - Obico AI Override`)
// const printProcessOverrideProfileName = computed(() => `${originalPrintProcessProfileName.value} - Obico AI Override`)
// const allPresetsSelected = computed(() => selectedMachine && selectedFilament && selectedPrintProcess)

// Watch messages to scroll to bottom
watchEffect(() => {
  scrollToBottom()
})

// Methods
function formatMarkdown(text) {
  return text.replace(/(\d\.)/g, '\n$1')
}

async function onUserMessageQuickAction(msg) {
  quickActions.value = []
  messages.value.push({ role: 'user', content: msg })
  await sendMessage()
}

async function onUserInput() {
  if (userInput.value.trim()) {
    messages.value.push({ role: 'user', content: userInput.value })
    userInput.value = ''
    await sendMessage()
  }
}

async function sendMessage() {
  thinking.value = true

  let slicerProfile = null
  let printProcessSlicerProfile = null

  // if (selectedFilament?.slicer_profile_url) {
  //   const encodedUrl = encodeURI(selectedFilament.slicer_profile_url).replace(/\+/g, '%2B')
  //   const response = await axios.get(encodedUrl)
  //   slicerProfile = response.data
  // }

  // if (selectedPrintProcess?.slicer_profile_url) {
  //   const encodedUrl = encodeURI(selectedPrintProcess.slicer_profile_url).replace(/\+/g, '%2B')
  //   const response = await axios.get(encodedUrl)
  //   printProcessSlicerProfile = response.data
  // }

  try {
    const response = await axios.post(urls.jusprintChatMessages(), {
      messages: messages.value,
      slicer_profile: {
        filament_json: slicerProfile,
        print_process_json: printProcessSlicerProfile,
      },
    })

    thinking.value = false
    const message = response.data.message
    messages.value.push({
      role: 'assistant',
      content: message.content,
      params: message.params,
    })

    const chatsApiUrl = urls.jusprintChats(jusprintCurrentChatId.value)
    const chatsApiVerb = jusprintCurrentChatId.value ? 'put' : 'post'
    const chatsApiResponse = await axios[chatsApiVerb](chatsApiUrl, {
      messages: JSON.stringify(messages.value),
      // machine_brand_name: selectedMachine?.machine_model?.brand?.name,
      // machine_name: selectedMachine?.name,
      // filament_name: selectedFilament?.name,
      // print_process_name: selectedPrintProcess?.name,
    })
    jusprintCurrentChatId.value = chatsApiResponse?.data?.id
    // store.commit('sliceSettingsStore/SET_JUSPRINT_CURRENT_CHAT_ID', chatsApiResponse?.data?.id)

    // if (message.params?.filament_json) {
    //   store.commit('sliceSettingsStore/SET_FILAMENT_PROFILE_OVERWRITE', message.params.filament_json)
    // }
    // if (message.params?.print_process_json) {
    //   store.commit('sliceSettingsStore/SET_PROFILE_OVERWRITE', message.params.print_process_json)
    // }

    populateQuickActionsBasedOnMessage(message)
  } catch (error) {
    console.error('Error sending message:', error)
    thinking.value = false
    messages.value.push({
      role: 'assistant',
      content: 'Sorry, there was an error processing your request.',
    })
  }
}

function changedParams(message) {
  return {
    ...message.params?.filament_json,
    ...message.params?.print_process_json,
  }
}

function scrollToBottom() {
  const chatMessageContainer = document.getElementById('chatMessageContainer')
  if (chatMessageContainer) {
    chatMessageContainer.scrollTop = chatMessageContainer.scrollHeight
  }
}

function initQuickActions() {
  const initQueryHints = [
    'Just do a standard print. Nothing special.',
    'I want to do a fast print. Draft quality.',
    'Print a strong part.',
    'My last print did not stick to the bed. Help me fix it.',
  ]

  const msgQuickActions = initQueryHints.map((hint) => ({
    message: hint,
    onClick: () => onUserMessageQuickAction(hint),
  }))
  quickActions.value = [
    {
      message: 'Select different machine, filament, or print process',
      onClick: () => emit('change-presets'),
    },
    ...msgQuickActions,
  ]
}

function populateQuickActionsBasedOnMessage(message) {
  quickActions.value = [
    {
      message: 'Slice with current settings',
      onClick: () => emit('slice-model'),
    },
  ]
}

// Lifecycle hook
onMounted(() => {
  initQuickActions()
})
</script>

<template>
  <div class="chat-container">
    <div ref="chatMessageContainer" class="chat-messages">
      <div class="message assistant mb-4">
        <div class="message-block">
          <div class="assistant-avatar">
            <i class="fas fa-robot"></i>
          </div>
          <div v-if="allPresetsSelected" class="message-content">
            <p>Welcome to Obico AI Slicer Assistant! Your current selections:</p>
            <div>- <strong>Machine:</strong> {{ selectedMachine?.name }}</div>
            <div>- <strong>Filament:</strong> {{ selectedFilament?.name }}</div>
            <div>- <strong>Print Process:</strong> {{ selectedPrintProcess?.name }}</div>
          </div>
          <div v-else class="message-content">
            <p>Welcome to Obico AI Slicer Assistant!</p>
            <div class="pt-2">
              You haven't selected a machine, filament, or print process yet. Please select them
              first and then come back to chat.
            </div>
          </div>
        </div>
      </div>

      <div v-for="(message, index) in messages" :key="index" class="message" :class="message.role">
        <div class="message-block">
          <div v-if="message.role === 'assistant'" class="assistant-avatar">
            <i class="fas fa-robot"></i>
          </div>
          <div class="message-content">
            <!-- <VueMarkdown :key="message.content">{{ formatMarkdown(message.content) }}</VueMarkdown> -->
             <div v-html="md.render(formatMarkdown(message.content))"></div>
             <!-- <div>{{ md.render(formatMarkdown(message.content)) }}</div> -->
            <div v-if="Object.keys(changedParams(message)).length > 0" class="pt-2">
              <div>I have changed the following parameters:</div>
              <div v-for="(value, key) in changedParams(message)" :key="key">
                - {{ key }}: {{ value }}
              </div>
            </div>
          </div>
        </div>
      </div>
      <div v-if="thinking" class="message assistant">
        <div class="message-block align-items-center">
          <div class="assistant-avatar">
            <i class="fas fa-robot"></i>
          </div>
          <div class="message-content">
            <div v-html="md.render('_Thinking..._')"></div>
          </div>
        </div>
      </div>
      <div v-if="allPresetsSelected">
        <div v-if="quickActions.length > 0" class="quick-action pt-2">
          <quick-action-button
            v-for="(action, index) in quickActions"
            :key="index"
            :message="action.message"
            @click="action.onClick"
          />
        </div>
      </div>
      <div v-else class="quick-action pt-2">
        <quick-action-button
          message="Select machine, filament, and print process first"
          @click="emit('change-presets')"
        />
      </div>
    </div>
    <div class="chat-input">
      <input
        v-model="userInput"
        type="text"
        :disabled="!allPresetsSelected"
        :placeholder="
          allPresetsSelected
            ? 'Type your message here...'
            : 'Select machine, filament, and print process first'
        "
        @keyup.enter="onUserInput"
      />
      <button class="send-button" :disabled="!allPresetsSelected" @click="onUserInput">
        <span class="send-icon">➜</span>
      </button>
    </div>
  </div>
</template>

<style>
.page-wrapper {
  position: relative;
  width: 100vw;
  height: 100vh;
  overflow: hidden;
}

.icon-btn-container {
  position: fixed;
  bottom: 5%;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  justify-content: flex-end;
  align-items: center;
  width: 80%;
  max-width: 33em;
  z-index: 10;
  margin-bottom: 2em;
}

.icon-btn-container .action-buttons {
  display: flex;
  gap: 12px;
}

.chat-container {
  display: flex;
  flex-direction: column;
  height: 100%;
  overflow-y: auto;
}

.chat-messages {
  flex-grow: 1;
  overflow-y: auto;
  padding: 1rem 0.5rem;
  display: flex;
  flex-direction: column;
  padding: 0 17px;
}

.message {
  border-radius: 18px;
  margin-bottom: 1.5rem;
}

.message .message-block {
  padding: 0.5rem;
  border-radius: 18px;
  line-height: 1.4;
  position: relative;
  color: black;
}

.message.user {
  align-self: flex-end;
  background-color: #dcf8c6;
}

.message.assistant .message-block {
  background-color: #fff;
  display: inline-flex;
  align-items: flex-start;
  max-width: 80%;
}

.quick-action {
  display: flex;
  flex-wrap: wrap;
  justify-content: flex-end;
  align-items: flex-end;
  width: 80%;
  margin-left: auto;
  gap: 0.5rem;
}

.assistant-avatar {
  width: 30px;
  height: 30px;
  background-color: #8e44ad;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-right: 8px;
  flex-shrink: 0;
}

.assistant-avatar i {
  color: white;
}

.assistant-avatar :deep(svg) { /* Add :deep() to target the svg inside the component */
  fill: white;
  width: 18px;
  height: 18px;
}

.chat-input {
  padding: 1rem;
  display: flex;
  align-items: center;
}

.chat-input input {
  flex-grow: 1;
  border: none;
  outline: none;
  padding: 0.5rem;
  font-size: 1rem;
}

.send-button {
  background-color: var(--color-primary);
  color: var(--color-text-primary);
  border: none;
  border-radius: 50%;
  width: 40px;
  height: 40px;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  margin-left: 0.5rem;
}

.send-icon {
  font-size: 1.2rem;
}

.message-content :deep(p) {
  margin: 0;
  padding: 0;
}

.message-content :deep(a) {
  color: var(--color-primary);
  text-decoration: underline;
  cursor: pointer;
}

.single-line-message {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.single-line-message a {
  display: inline;
  white-space: nowrap;
}

.button-container {
  display: flex;
  justify-content: flex-end;
}

.align-items-center {
  align-items: center;
}

strong {
  font-weight: bold;
}
</style>
