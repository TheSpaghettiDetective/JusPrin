<script setup>
import { defineProps, defineEmits } from 'vue'

const props = defineProps({
  message: {
    type: String,
    required: true,
  },
})

const emit = defineEmits(['click'])

function handleClick(event) {
  emit('click', event)
}
</script>
<template>
  <a href="#" class="btn btn-outline-primary" @click="handleClick">
    {{ props.message }}
  </a>
</template>

<style>
.btn {
  width: auto;
  max-width: 100%;
  white-space: normal;
  text-align: center;
  padding: 0.5rem 1rem;
  hyphens: auto;
  word-wrap: break-word;
  overflow-wrap: break-word;
  color: #03DAC5;
  border-radius: 21px;
  border: 1px solid #03DAC5;
}
</style>
